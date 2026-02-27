# XIAOZHI_MQTTUDP_API_FLOW

MQTT + UDP hybrid protocol flow and JSON structures used by the Byte-90 firmware.
This document is inferred from the current codebase and should be kept in sync
with `lib/network/MQTTClient.*`, `lib/services/ApiClient.*`, and
`src/ApplicationServices.cpp`.

**Last Updated:** 2026-02-11

---

## Table of Contents

1. [1. Overview](#1-overview)
2. [2. Connection Flow](#2-connection-flow)
3. [3. MQTT Control Channel](#3-mqtt-control-channel)
4. [4. UDP Audio Channel](#4-udp-audio-channel)
5. [5. JSON Message Types](#5-json-message-types)
6. [6. MCP Initialization (MQTT)](#6-mcp-initialization-mqtt)
---

## 1. Overview

- **Control:** MQTT for JSON messages
- **Audio:** UDP for encrypted Opus frames
- **Security:** AES-CTR for UDP audio
- **Usage:** Enabled when `USE_MQTT_PROTOCOL=1`

---

## 2. Connection Flow

1) Device connects to MQTT broker
2) Device sends `hello` with `transport: "udp"`
3) Server responds with UDP endpoint + AES key/nonce
4) Device opens UDP audio channel
5) JSON control continues over MQTT while audio streams over UDP

**Note:** MQTT control messages are exchanged over MQTT topics (publish topic `device-server` and per-device p2p subscribe topic). Audio streams over UDP after the server hello is received.

---

## 3. MQTT Control Channel

### Device → Server Hello

```json
{
  "type": "hello",
  "version": 3,
  "transport": "udp",
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

### Server → Device Hello (includes UDP info)

```json
{
  "type": "hello",
  "transport": "udp",
  "session_id": "xxx",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  },
  "udp": {
    "server": "192.168.1.100",
    "port": 8888,
    "encryption": "aes-128-ctr",
    "key": "0123456789ABCDEF0123456789ABCDEF",
    "nonce": "0123456789ABCDEF0123456789ABCDEF"
  }
}
```

---

## 4. UDP Audio Channel

### Packet Header

```
|type 1byte|flags 1byte|payload_len 2bytes|ssrc 4bytes|timestamp 4bytes|sequence 4bytes|
|payload payload_len bytes|
```

- `type`: fixed 0x01
- `payload_len`, `timestamp`, `sequence`: network byte order
- `payload`: encrypted Opus data

### Encryption

- **Algorithm:** AES-CTR
- **Key:** 128-bit (from server hello)
- **Nonce:** 128-bit (from server hello)

### Sequence Handling

- Sender increments `local_sequence_`
- Receiver validates `remote_sequence_`
- Out-of-order packets are logged/dropped

---

## 5. JSON Message Types

Message types are aligned with WebSocket and use the same payloads:

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

**Note:** Current firmware does not implement real wake word detection. The detect message is a workaround to prompt an initial server greeting before the normal conversation flow starts.

**Abort**
```json
{
  "session_id": "xxx",
  "type": "abort",
  "reason": "user_stopped"
}
```

**MCP**
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

**Goodbye**
```json
{
  "session_id": "xxx",
  "type": "goodbye"
}
```

Server → device messages mirror the WebSocket flow (STT/TTS/LLM/system).

---

## 6. MCP Initialization (MQTT)

MCP runs over the MQTT control channel with `type: "mcp"` and JSON-RPC 2.0
payloads. The initialize → tools/list → tools/call sequence matches the
WebSocket flow.
