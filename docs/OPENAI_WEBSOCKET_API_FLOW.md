# OPENAI_WEBSOCKET_API_FLOW

OpenAI Realtime WebSocket flow and JSON structures used by the Byte-90 firmware.
This document is inferred from the current codebase and should be kept in sync
with `lib/services/OpenAIWebsocket.*`, `lib/services/OpenAIApiClient.*`,
`lib/audio/AudioService.*`, `lib/system/DeviceConfig.h`, and
`src/ApplicationServices.cpp`.

**Last Updated:** 2026-01-10

---

## Table of Contents

1. [1. Overview](#1-overview)
2. [2. Connection Flow](#2-connection-flow)
3. [3. WebSocket Handshake Headers](#3-websocket-handshake-headers)
4. [4. Session Update](#4-session-update)
5. [5. JSON Message Types](#5-json-message-types)
6. [6. Audio Payloads](#6-audio-payloads)
7. [7. Tool Calling (OpenAI Function Calls)](#7-tool-calling-openai-function-calls)
---

## 1. Overview

- **Transport:** WebSocket Secure (wss://) to OpenAI Realtime API
- **Audio:** PCM16, 24 kHz, mono (input + output)
- **Control:** JSON messages over WebSocket text frames
- **Audio Upload:** Base64 PCM in `input_audio_buffer.append`
- **Audio Output:** Base64 PCM in `response.output_audio.delta`

---

## 2. Connection Flow

1) Device loads OpenAI API key from NVS
2) Device connects to `wss://api.openai.com/v1/realtime?model=<model>`
3) Server sends `session.created`
4) Device sends `session.update` (model/voice/audio/VAD/tools)
5) Server sends `session.updated`
6) PCM capture begins (no `listen` message in OpenAI pipeline)

---

## 3. WebSocket Handshake Headers

The OpenAI client sets a single auth header:

```
Authorization: Bearer <api_key>
```

---

## 4. Session Update

Device sends session configuration after `session.created`.

```json
{
  "type": "session.update",
  "session": {
    "type": "realtime",
    "model": "gpt-realtime-mini-2025-10-06",
    "output_modalities": ["audio"],
    "audio": {
      "input": {
        "format": { "type": "audio/pcm", "rate": 24000 },
        "noise_reduction": { "type": "far_field" },
        "turn_detection": {
          "type": "server_vad",
          "idle_timeout_ms": 6000,
          "threshold": 0.30,
          "prefix_padding_ms": 600,
          "silence_duration_ms": 500,
          "create_response": true,
          "interrupt_response": true
        }
      },
      "output": {
        "format": { "type": "audio/pcm", "rate": 24000 },
        "voice": "verse"
      }
    },
    "instructions": "<OPENAI_REALTIME_INSTRUCTIONS>",
    "tool_choice": "none",
    "tools": []
  }
}
```

**Notes:**
- VAD parameters are configurable via `OpenAIWebsocket` setters.
- When tools are registered, `tools[]` is filled via `OpenAiAdapter` and
  `tool_choice` becomes `auto`.
- `output_modalities` is audio-only in the current codebase.

---

## 5. JSON Message Types

### Device → Server

**Session update**
```json
{ "type": "session.update", "session": { ... } }
```

**Input audio append (base64 PCM16)**
```json
{
  "type": "input_audio_buffer.append",
  "audio": "<base64_pcm16>"
}
```

**Input audio clear**
```json
{ "type": "input_audio_buffer.clear" }
```

**Create response**
```json
{ "type": "response.create" }
```

**Cancel response**
```json
{
  "type": "response.cancel",
  "response_id": "resp_..."
}
```

**Text input (user message)**
```json
{
  "type": "conversation.item.create",
  "item": {
    "type": "message",
    "role": "user",
    "content": [
      { "type": "input_text", "text": "hello" }
    ]
  }
}
```

**Tool output (function_call_output)**
```json
{
  "type": "conversation.item.create",
  "item": {
    "type": "function_call_output",
    "call_id": "call_...",
    "output": "{\"ok\":true}"
  }
}
```

### Server → Device

**Session created/updated**
```json
{ "type": "session.created" }
```

```json
{ "type": "session.updated" }
```

**VAD events**
```json
{
  "type": "input_audio_buffer.speech_started",
  "event_id": "...",
  "item_id": "...",
  "audio_start_ms": 0
}
```

```json
{
  "type": "input_audio_buffer.speech_stopped",
  "event_id": "...",
  "item_id": "...",
  "audio_end_ms": 1234
}
```

```json
{
  "type": "input_audio_buffer.committed",
  "item_id": "...",
  "previous_item_id": "..."
}
```

**Response lifecycle**
```json
{
  "type": "response.created",
  "event_id": "...",
  "response": { "id": "resp_...", "status": "in_progress" }
}
```

```json
{
  "type": "response.output_text.delta",
  "delta": "Hello"
}
```

```json
{
  "type": "response.output_text.done",
  "text": "Hello world"
}
```

```json
{
  "type": "response.output_audio.delta",
  "delta": "<base64_pcm16>"
}
```

```json
{ "type": "response.output_audio.done" }
```

```json
{
  "type": "response.function_call_arguments.done",
  "call_id": "call_...",
  "name": "device_control",
  "arguments": "{\"state\":true}"
}
```

```json
{
  "type": "response.done",
  "event_id": "...",
  "response": {
    "id": "resp_...",
    "status": "completed",
    "output": [
      {
        "content": [
          { "transcript": "..." }
        ]
      }
    ],
    "usage": {
      "input_tokens": 0,
      "output_tokens": 0,
      "total_tokens": 0
    }
  }
}
```

**Buffer clear ack + cancel ack**
```json
{ "type": "input_audio_buffer.cleared", "event_id": "..." }
```

```json
{ "type": "response.cancelled", "response_id": "resp_..." }
```

**Input audio transcription (optional)**
```json
{
  "type": "conversation.item.input_audio_transcription.completed",
  "item_id": "...",
  "content_index": 0,
  "transcript": "..."
}
```

**Error**
```json
{
  "type": "error",
  "error": { "code": "...", "message": "...", "event_id": "..." }
}
```

---

## 6. Audio Payloads

### Capture (Device → OpenAI)

- Capture at 16 kHz, mono.
- Resample to 24 kHz in `AudioService::processOpenAICapture`.
- Base64 encode PCM16 and send via `input_audio_buffer.append`.

### Playback (OpenAI → Device)

- Receive base64 PCM in `response.output_audio.delta`.
- Decode to PCM16 and push into the OpenAI ring buffer.
- `AudioService::startPcmPlayback()` pulls PCM and writes to the codec.
- OpenAI pipeline sets `SYSTEM_STATE_SPEAKING` immediately on speech start (no deferred speaking),
  to avoid missing auto-capture resume on rapid responses/cancels.

---

## 7. Tool Calling (OpenAI Function Calls)

- Tool schemas are generated from the local MCP registry via `OpenAiAdapter`.
- OpenAI sends `response.function_call_arguments.done` with `name` + `arguments`.
- Device executes the tool locally and replies with
  `conversation.item.create` (type `function_call_output`).
- Device then sends `response.create` to request the model to continue.
