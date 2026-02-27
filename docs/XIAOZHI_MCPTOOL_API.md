# XIAOZHI_MCPTOOL_API

**Known issue (backend/MQTT):** The server can randomly fail to invoke `tools/call` because it does not receive or register `tools/list` despite logs claiming registration succeeded. When this happens, the server reports that no tools are available, will not send any `tools/call` responses, and no tools can be run.

MCP (Model Context Protocol) tool discovery and invocation flow for Byte-90.
This document is based on the current codebase and is transport-agnostic (WebSocket/MQTT).

**Last Updated:** 2026-01-10

---

## Table of Contents

1. [1. Overview](#1-overview)
2. [2. MCP Envelope (Transport Wrapper)](#2-mcp-envelope-transport-wrapper)
3. [3. JSON-RPC Methods](#3-json-rpc-methods)
4. [4. Tool Registration (Device Side)](#4-tool-registration-device-side)
5. [5. Tool List Pagination and User-Only Tools](#5-tool-list-pagination-and-user-only-tools)
6. [6. Tool Call Results and Errors](#6-tool-call-results-and-errors)
7. [7. Runtime Handling and Queues](#7-runtime-handling-and-queues)
8. [Notes](#notes)
---

## 1. Overview

- MCP uses **JSON-RPC 2.0** payloads carried inside a transport envelope.
- Supported methods: `initialize`, `tools/list`, `tools/call`.
- Device-side tools are registered via `McpServer` and `McpToolRegistry`.
- Tool results are returned as JSON with `content[]` and `isError` fields.

**Code references:**
- `lib/mcp/McpServer.*`
- `lib/mcp/McpTool.*`
- `lib/mcp/McpToolRegistry.*`
- `src/ApplicationServices.cpp`
- `lib/services/ApiClient.*`

---

## 2. MCP Envelope (Transport Wrapper)

MCP messages are wrapped in a transport envelope:

```json
{
  "session_id": "...",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": { "cursor": "" },
    "id": 10001
  }
}
```

**Notes:**
- `session_id` may be empty during early control-plane setup.
- The same envelope is used for WebSocket and MQTT control channels.

---

## 3. JSON-RPC Methods

### 3.1 initialize (Client → Device)

```json
{
  "jsonrpc": "2.0",
  "method": "initialize",
  "params": {
    "capabilities": {
      "vision": {
        "url": "http://api.xiaozhi.me/vision/explain",
        "token": "..."
      }
    }
  },
  "id": 1
}
```

**Response (Device → Client):**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": { "tools": {} },
    "serverInfo": {
      "name": "BYTE-90",
      "version": "..."
    }
  }
}
```

### 3.2 tools/list (Client → Device)

```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": {
    "cursor": "",
    "withUserTools": false
  },
  "id": 2
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "self.audio_speaker.set_volume",
        "description": "Set the volume...",
        "inputSchema": {
          "type": "object",
          "properties": {
            "volume": {"type": "integer", "minimum": 0, "maximum": 100}
          },
          "required": ["volume"]
        }
      }
    ],
    "nextCursor": "optional-tool-name"
  }
}
```

### 3.3 tools/call (Client → Device)

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.audio_speaker.set_volume",
    "arguments": { "volume": 42 }
  },
  "id": 3
}
```

---

## 4. Tool Registration (Device Side)

Tools are registered at startup via `McpToolRegistry::buildDeviceToolRegistry()`.

```cpp
mcp->addTool(
    "self.audio_speaker.set_volume",
    "Set speaker volume",
    PropertyList({ Property("volume", PROPERTY_TYPE_INTEGER, 0, 100) }),
    [mcp](PropertyList& params) -> ReturnValue {
        // ... apply volume ...
        return true;
    }
);
```

**Key points:**
- Tool names are hierarchical (e.g., `self.audio_speaker.set_volume`).

### `self.audio_speaker.mute`
**Description:** Mute audio output (persists across reboots).

**Arguments:**
```json
{}
```

### `self.audio_speaker.unmute`
**Description:** Unmute audio output (restores previous volume).

**Arguments:**
```json
{}
```
- `PropertyList` defines input schema, required fields, and defaults.
- User-only tools are registered via `addUserOnlyTool()` and are hidden unless
  `withUserTools=true` is set in `tools/list`.

**Notable tools:**
- `self.set_timezone`: persist a default `timezone_name` for MCP tools.
- `self.set_location`: persist a default `location` for MCP tools.

---

## 5. Tool List Pagination and User-Only Tools

`tools/list` supports pagination and optional user-only tools:

- `cursor`: tool name to resume after
- `nextCursor`: returned when payload exceeds `MCP_MAX_PAYLOAD_SIZE`
- `withUserTools`: include tools marked with `annotations.audience = ["user"]`

---

## 6. Tool Call Results and Errors

### Success Result

`McpTool::call()` returns a JSON object shaped like:

```json
{
  "content": [
    { "type": "text", "text": "true" }
  ],
  "isError": false
}
```

### Error Result

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "error": {
    "code": -32601,
    "message": "Unknown tool: self.non_existent_tool"
  }
}
```

---

## 7. Runtime Handling and Queues

- Incoming MCP requests are queued in `ApplicationServices` (`_mcp_tool_queue`).
- Worker task `mcp_tool` parses JSON, calls `McpServer::parseMessage()`, and
  sends the JSON-RPC response via `ApiClient::sendMcpResponse()`.
- Tools list logging is reported when `tools/list` responses are generated.

---

## Notes

- MCP is transport-agnostic; the envelope is identical for WebSocket and MQTT.
- OpenAI Realtime uses `OpenAiAdapter` to bridge OpenAI function calls to local MCP tools.
