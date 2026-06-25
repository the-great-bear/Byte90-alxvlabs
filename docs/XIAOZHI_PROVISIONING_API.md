# Device Provisioning and Activation System Documentation

## Overview

The provisioning system retrieves device configuration (MQTT/WebSocket, server time, firmware info) and, when required, activates devices before full operation. Activation uses a challenge-response authentication scheme with HMAC-SHA256 when a hardware serial number is available, and falls back to a V1 payload when it is not.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Activation Flow](#activation-flow)
4. [Security Mechanism](#security-mechanism)
5. [Implementation Details](#implementation-details)
6. [API Reference](#api-reference)
7. [Troubleshooting](#troubleshooting)
---

### Key Features

- **Dual-Mode Support**: V1 (no serial number) and V2 (HMAC-based) activation
- **Challenge-Response Authentication**: Unique server challenges prevent replay attacks
- **Hardware Security**: eFuse-stored serial numbers and hardware HMAC engine
- **Automatic Retry**: Activation retries in a bounded loop per provisioning check
- **User-Friendly**: Visual and audio feedback for activation codes

---

## Architecture

### Components

#### 1. TenclassClient Class
**Location**: `lib/services/TenclassClient.cpp`, `lib/services/TenclassClient.h`

**Responsibilities**:
- Makes OTA request to retrieve activation data
- Parses activation code, challenge, and server time
- Generates HMAC-based activation payload via `ApiClient`
- Sends activation requests to server
- Handles V1/V2 activation modes

**Key Methods**:
- `checkConfig()` - Sends device info to OTA endpoint
- `parseActivation()` - Extracts activation code/challenge
- `getActivationPayload()` - Builds V1 or V2 activation payload
- `Activate()` - Sends activation request to `/activate` endpoint

#### 2. ProvisioningManager Class
**Location**: `src/ProvisioningManager.cpp`, `src/ProvisioningManager.h`

**Responsibilities**:
- Owns config check task lifecycle
- Triggers activation attempts when activation data is present
- Updates provisioning readiness flags

#### 3. Application/UI
**Location**: `src/Application.cpp`, `src/ApplicationUI.cpp`

**Responsibilities**:
- Displays activation message/code when required
- Clears activation screen when resolved

---

## Activation Flow

### Phase 1: OTA Version Check

```
1. Device boots → State: SYSTEM_STATE_ACTIVATING (if activation required)
2. ProvisioningManager::runConfigCheck() calls TenclassClient::checkConfig()
3. Server responds with JSON:
   - firmware version info
   - activation data (if device not activated)
   - MQTT/WebSocket config
   - server time
```

**OTA Request Payload** (device info)
Built by `ApiClient::buildDeviceInfo()` and sent to the OTA URL:

```json
{
  "version": 2,
  "language": "zh-CN",
  "flash_size": 8388608,
  "psram_size": 8388608,
  "minimum_free_heap_size": 142336,
  "mac_address": "cc:ba:97:00:f0:ac",
  "uuid": "00000000-0000-0000-0000-ccba9700f0ac",
  "chip_model_name": "ESP32-S3",
  "chip_info": {
    "model": 9,
    "cores": 2,
    "revision": 0,
    "features": 50
  },
  "application": {
    "name": "xiaozhi-esp32",
    "version": "2.0.0",
    "compile_time": "2026-02-02T10:30:00Z",
    "idf_version": "v5.1.2",
    "elf_sha256": "..."
  },
  "partition_table": [
    {"label":"nvs","type":1,"subtype":2,"address":36864,"size":16384}
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
    "type": "byte-90",
    "name": "byte-90",
    "ssid": "mywifi",
    "rssi": -58,
    "channel": 6,
    "ip": "192.168.x.x",
    "mac": "aa:bb:cc:dd:ee:ff"
  },
  "firmware_version": "3.1.0"
}
```

**OTA Request Payload (Byte-90 example from logs)**
```json
{
  "version": 2,
  "language": "en-US",
  "flash_size": 8388608,
  "psram_size": 8385239,
  "minimum_free_heap_size": 154200,
  "mac_address": "1c:db:d4:75:88:cc",
  "uuid": "d2d13eee-bf94-4f19-8132-c690d8dea35d",
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
    "elf_sha256": "5c234cd60d3e3e9c0901db3c6cb87acc931de2016b63f507e694908d8aac03ee"
  },
  "partition_table": [
    {"label":"nvs","type":1,"subtype":2,"address":36864,"size":16384},
    {"label":"otadata","type":1,"subtype":0,"address":53248,"size":8192},
    {"label":"phy_init","type":1,"subtype":1,"address":61440,"size":4096},
    {"label":"ota_0","type":0,"subtype":16,"address":65536,"size":2555904},
    {"label":"ota_1","type":0,"subtype":17,"address":2621440,"size":2621440},
    {"label":"assets","type":1,"subtype":130,"address":5242880,"size":3145728}
  ],
  "ota": {"label":"ota_0"},
  "display": {"monochrome": false, "width": 128, "height": 128},
  "board": {
    "type": "BYTE-90",
    "name": "BYTE-90",
    "ssid": "2cats",
    "rssi": -36,
    "channel": 1,
    "ip": "192.168.86.26",
    "mac": "1c:db:d4:75:88:cc"
  },
  "firmware_version": "3.1.0"
}
```

**OTA Request Headers** (from `TenclassClient::makeRequest()`):
- `Activation-Version`: `2` if serial number is present, else `1`
- `Device-Id`: MAC address
- `Client-Id`: device UUID
- `Serial-Number`: only if present
- `User-Agent`: from `DeviceConfig`
- `Accept-Language`: `DEFAULT_LANGUAGE`
- `Content-Type`: `application/json`

**OTA Response** (example when activation needed):
```json
{
  "activation": {
    "code": "834177",
    "message": "xiaozhi.me\n834177",
    "challenge": "d2e6638d-8378-4ef7-9599-1575941a556d",
    "timeout_ms": 30000
  },
  "firmware": {
    "version": "3.1.0",
    "url": "https://..."
  },
  "websocket": {
    "token": "...",
    "host": "api.tenclass.net",
    "path": "/v1",
    "use_ssl": true,
    "port": 443,
    "version": 1
  },
  "server_time": {
    "timestamp": 1764722713894,
    "timezone_offset": -300
  }
}
```

**OTA Response (example when activation is not needed)**
```json
{
  "mqtt": {
    "endpoint": "mqtt.xiaozhi.me",
    "client_id": "GID_test@@@1c_db_d4_75_88_cc@@@d2d13eee-bf94-4f19-8132-c690d8dea35d",
    "username": "eyJpcCI6IjEzNC4yMzEuNTYuODkifQ==",
    "password": "CZCeXpIdXlpZyR0aWjcTAh4IhbWekanA6nXZfcpIY6U=",
    "publish_topic": "device-server",
    "subscribe_topic": "null"
  },
  "websocket": {
    "url": "wss://api.tenclass.net/xiaozhi/v1/",
    "token": "test-token"
  },
  "server_time": {
    "timestamp": 1770815853593,
    "timezone_offset": -300
  },
  "firmware": {
    "version": "esp-idf: v4.4.7 38eeba213a",
    "url": ""
  }
}
```

### Phase 2: Activation Code Display

**Location**: `src/Application.cpp`, `src/ApplicationUI.cpp`

When activation is required:
- The UI shows `activation.message` and `activation.code`
- Device remains in activation state until successful

### Phase 3: Activation Request Loop

**Location**: `src/ProvisioningManager.cpp`

When activation data is present:
- Runs a bounded loop (10 attempts) per provisioning check
- `Activate()` returns:
  - `ESP_OK` → success, stop attempts
  - `ESP_ERR_TIMEOUT` → 202 Accepted, retry after delay
  - other → retry after longer delay

---

## Security Mechanism

### V2 Activation (HMAC)

If a serial number is present in eFuse (`ESP_EFUSE_USER_DATA`), the device generates:
- `hmac = HMAC_SHA256(challenge)` using `HMAC_KEY0`
- Payload fields: `algorithm`, `serial_number`, `challenge`, `hmac`

### V1 Activation (Legacy)

If serial number or challenge is missing:
- Send an empty JSON object `{}`
- Activation version header is set to `1`

---

## Implementation Details

### Activation Request

**Headers**:
- `Device-Id`: MAC address (lowercased)
- `Client-Id`: Device UUID
- `Activation-Version`: `2` if serial is available, else `1`
- `Serial-Number`: only for V2
- `Content-Type`: `application/json`

**URL**:
- Activation endpoint is derived from OTA URL and appends `/activate`

### Activation Response Codes

| Code | Meaning | Behavior |
|------|---------|----------|
| `200 OK` | Activation successful | Clears activation flags, proceed |
| `202 Accepted` | Pending user activation | Retry after short delay |
| Other | Activation failed | Log error, retry after longer delay |

---

## API Reference

### `String getActivationPayload()`

Returns JSON payload for activation:
- V2: `{"algorithm":"hmac-sha256","serial_number":"...","challenge":"...","hmac":"..."}`
- V1: `{}`

### `bool HasActivationCode() const`

Returns `true` if an activation code is present.

### `String GetActivationCode() const`

Returns activation code string (e.g., `"834177"`).

### `String GetActivationMessage() const`

Returns activation message string shown to the user.

---

## Troubleshooting

### 1. Activation loops (202 Accepted)

**Cause**: User has not completed activation on the server.
**Fix**: Complete activation using the displayed code.

### 2. Activation fails (non-200/202)

**Cause**: Server rejected payload or configuration.
**Fix**: Verify server URL and device registration.

### 3. Missing serial number

**Cause**: eFuse USER_DATA not available or blank.
**Behavior**: Device uses V1 activation payload.
