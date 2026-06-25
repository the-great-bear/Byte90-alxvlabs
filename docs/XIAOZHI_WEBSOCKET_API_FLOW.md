# XIAOZHI_WEBSOCKET_API_FLOW

WebSocket protocol flow and JSON structures used by the Byte-90 firmware.
This document is inferred from the current codebase and should be kept in sync
with `lib/services/TenclassWebsocket.*`, `lib/network/WebsocketClient.*`,
`lib/services/ApiClient.*`, and `src/ApplicationServices.cpp`.

**Last Updated:** 2026-02-11

---

## Table of Contents

1. [1. Overview](#1-overview)
2. [2. Connection Flow](#2-connection-flow)
3. [3. Hello Handshake](#3-hello-handshake)
4. [4. JSON Message Types](#4-json-message-types)
5. [5. Binary Audio Protocols](#5-binary-audio-protocols)
6. [6. MCP Initialization (WebSocket)](#6-mcp-initialization-websocket)
---

## 1. Overview

- **Transport:** WebSocket Secure (wss://)
- **Audio:** Opus, 16 kHz capture, 24 kHz playback (server side)
- **Control:** JSON messages over WebSocket text frames
- **Binary Audio:** WebSocket binary frames (v1/v2)

---

## 2. Connection Flow

1) Provisioning returns WebSocket endpoint + token
2) Device opens audio channel via `TenclassWebsocket::OpenAudioChannel()`
3) Device sends `hello`
4) Server responds with `hello` + `session_id`
5) Audio channel opens and callbacks fire
6) On user button press, firmware uses a **synthetic wake word detect** flow to trigger a greeting:
   - Send `listen` `start` to enter listening mode.
   - After a short delay, send `listen` `detect` with a fixed text string.

**Note:** Current firmware does not implement real wake word detection. The detect message is a workaround to prompt an initial server greeting before the normal conversation flow starts.

### Serial Interface Ready (Device → Host)

```json
{
  "success": true,
  "message": "BYTE-90 Serial Interface Ready",
  "ssid": "",
  "rssi": 0,
  "signal_strength": "NOT_CONNECTED",
  "connected": false,
  "state": "UPDATE_MODE",
  "system_mode": "Update Mode",
  "networks": null
}
```

### Provisioning Response Object (Server → Client)

Captured from live OTA provisioning logs (`https://api.tenclass.net/xiaozhi/ota/`):

```json
{
  "mqtt": {
    "endpoint": "mqtt.xiaozhi.me",
    "client_id": "GID_test@@@1c_db_d4_75_88_cc@@@792bf676-bed1-4fae-8a7d-3bbc4f5d3e92",
    "username": "eyJpcCI6IjEzNC4yMzEuNTYuODkifQ==",
    "password": "CEURb4S/Klmqml/9AULYPu9ScIg41plR3RMTvZ3v2DE=",
    "publish_topic": "device-server",
    "subscribe_topic": "null"
  },
  "websocket": {
    "url": "wss://api.tenclass.net/xiaozhi/v1/",
    "token": "test-token"
  },
  "server_time": {
    "timestamp": 1770499372521,
    "timezone_offset": -300
  },
  "firmware": {
    "version": "esp-idf: v4.4.7 38eeba213a",
    "url": ""
  }
}
```

### Provisioning Request Payload (Client → Server)

Captured from live OTA provisioning logs (`https://api.tenclass.net/xiaozhi/ota/`):

```json
{
  "version": 2,
  "language": "en-US",
  "flash_size": 8388608,
  "psram_size": 8385239,
  "minimum_free_heap_size": 154200,
  "mac_address": "1c:db:d4:75:88:cc",
  "uuid": "792bf676-bed1-4fae-8a7d-3bbc4f5d3e92",
  "chip_model_name": "ESP32-S3",
  "chip_info": {
    "model": 9,
    "cores": 2,
    "revision": 0,
    "features": 18
  },
  "application": {
    "name": "arduino-lib-builder",
    "version": "esp-idf: v4.4.7 38eeba213a",
    "compile_time": "Mar  5 2024T12:12:53Z",
    "idf_version": "v4.4.7-dirty",
    "elf_sha256": "619a2d46fda6f41043b91148093bd8c51a8af2eed4928a0ff885b02d99561b69"
  },
  "partition_table": [
    {
      "label": "nvs",
      "type": 1,
      "subtype": 2,
      "address": 36864,
      "size": 16384
    },
    {
      "label": "otadata",
      "type": 1,
      "subtype": 0,
      "address": 53248,
      "size": 8192
    },
    {
      "label": "phy_init",
      "type": 1,
      "subtype": 1,
      "address": 61440,
      "size": 4096
    },
    {
      "label": "ota_0",
      "type": 0,
      "subtype": 16,
      "address": 65536,
      "size": 2555904
    },
    {
      "label": "ota_1",
      "type": 0,
      "subtype": 17,
      "address": 2621440,
      "size": 2621440
    },
    {
      "label": "assets",
      "type": 1,
      "subtype": 130,
      "address": 5242880,
      "size": 3145728
    }
  ],
  "ota": {
    "label": "ota_0"
  },
  "display": {
    "monochrome": false,
    "width": 128,
    "height": 128
  },
  "board": {
    "type": "BYTE-90",
    "name": "BYTE-90",
    "ssid": "2cats",
    "rssi": -39,
    "channel": 1,
    "ip": "192.168.86.26",
    "mac": "1c:db:d4:75:88:cc"
  },
  "firmware_version": "3.1.0"
}
```

### WebSocket Handshake Headers

The client sets standard WebSocket headers plus auth (values come from
provisioning `websocket.url` + `websocket.token`):

Current observed provisioning value:
- `wss://api.tenclass.net/xiaozhi/v1/`

```
GET <ws_path_from_provisioning> HTTP/1.1
Host: <ws_host_from_provisioning>:<ws_port>
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <generated>
Sec-WebSocket-Version: 13
Authorization: Bearer <token_from_provisioning>
Protocol-Version: <ws_version>   # default: 1
Device-Id: <device_mac>
Client-Id: <uuid>
```

Example for the current provisioning URL:

```
GET /xiaozhi/v1/ HTTP/1.1
Host: api.tenclass.net:443
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <generated>
Sec-WebSocket-Version: 13
Authorization: Bearer <token_from_provisioning>
Protocol-Version: <ws_version>   # default: 1
Device-Id: <device_mac>
Client-Id: <uuid>
```

---

## 3. Hello Handshake

### Device → Server

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "features": {
    "mcp": true,
    "aec": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

`hello.version` uses the same value as the `Protocol-Version` header
(`websocket.version` from provisioning, default `1`).

### Server → Device

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "xxx",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

---

## 4. JSON Message Types

### Device → Server

**Listen (start/stop)**
```json
{
  "session_id": "xxx",
  "type": "listen",
  "state": "start",
  "mode": "auto"
}
```

**Listen (detect / synthetic wake word)**
```json
{
  "session_id": "xxx",
  "type": "listen",
  "state": "detect",
  "text": "What's up"
}
```

**Abort**
```json
{
  "session_id": "xxx",
  "type": "abort",
  "reason": "user_stopped"
}
```

**Goodbye**
```json
{
  "session_id": "xxx",
  "type": "goodbye"
}
```

**MCP (JSON-RPC 2.0 envelope)**
```json
{
  "session_id": "xxx",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {"tools": []}
  }
}
```

### Server → Device

**STT**
```json
{
  "type": "stt",
  "text": "Hello world",
  "session_id": "xxx"
}
```

**TTS (start/sentence/stop)**
```json
{
  "type": "tts",
  "state": "start",
  "sample_rate": 24000,
  "session_id": "xxx"
}
```

```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "Hi there",
  "session_id": "xxx"
}
```

```json
{
  "type": "tts",
  "state": "stop",
  "session_id": "xxx"
}
```

**LLM emotion**
```json
{
  "type": "llm",
  "text": "😊",
  "emotion": "happy",
  "session_id": "xxx"
}
```

---

## 5. Binary Audio Protocols

**Version 1:** Raw Opus payload as WebSocket binary message.

**Version 2:** Binary header with timestamp (server-side AEC).

```c
struct __attribute__((packed)) BinaryProtocol2 {
    uint16_t version;      // 2
    uint16_t type;         // 0: OPUS, 1: JSON
    uint32_t reserved;     // 0
    uint32_t timestamp;    // ms, for AEC
    uint32_t payload_size; // bytes
    uint8_t payload[];     // Opus or JSON
};
```

**Note:** v3 is not present in the current codebase.

---

## 6. MCP Initialization (WebSocket)

MCP is sent through the WebSocket JSON channel with `type: "mcp"` and a
JSON-RPC 2.0 payload. The flow is:

1) Server sends `initialize`
2) Device responds with capabilities
3) Server sends `tools/list`
4) Device returns tool registry

See `lib/services/TenclassWebsocket.cpp` and `lib/mcp/*` for handling.
