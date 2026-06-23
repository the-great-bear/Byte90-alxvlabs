# Byte90 Local HTTP API

The Byte90 runs an HTTP server (CaptivePortal) accessible on both:
- **Station IP** — whatever IP your router assigns (check via serial: `GET_STATUS`)
- **AP IP** — `192.168.4.1` when connected to `BYTE90-Config` (password: `12345678`)

## Endpoints Active in Firmware 3.1.0

Only these endpoints respond with JSON on the station IP. All others fall through to the portal HTML.

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | WiFi connection status |
| GET | `/api/timezone/list` | List of available timezone names |

## Endpoints in Source (not yet active in 3.1.0 binary)

These routes are registered in `CaptivePortal::setupRoutes()` and will work once firmware is updated:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/effects/status` | Current effect and tint state |
| POST | `/api/effects` | Apply effect/tint (`{"effect":"scanlines","tint":"blue"}`) |
| GET | `/api/audio/status` | Current volume |
| POST | `/api/audio` | Set volume (`{"volume":70}`) |
| POST | `/api/audio/reset` | Reset audio to defaults |
| GET | `/api/clock/status` | Clock mode state |
| POST | `/api/clock` | Apply clock settings |
| GET | `/api/timezone/status` | Saved timezone |
| POST | `/api/timezone` | Save timezone |
| POST | `/api/timezone/clear` | Clear saved timezone |
| GET | `/api/location/status` | Saved location |
| POST | `/api/location` | Save location |
| POST | `/api/location/clear` | Clear saved location |
| GET | `/api/scan` | Scan for WiFi networks |
| POST | `/api/configure` | Connect to WiFi (`{"ssid":"...","password":"..."}`) |
| POST | `/api/disconnect` | Disconnect from WiFi |

## Effects API Payload (once active)

```json
POST /api/effects
{ "effect": "none|scanlines|dot_matrix|glitch", "tint": "none|green|blue|yellow" }
```

Note: red tint is **not implemented** in `RetroTints.h`. Available tint colors: green, blue, yellow.

## Serial Interface

The device exposes a serial interface at 921600 baud on `/dev/ttyACM0` (USB-JTAG).

Command format: `COMMAND` or `COMMAND:data` (plain text, newline-terminated — NOT JSON).

| Command | Data | Description |
|---------|------|-------------|
| `GET_STATUS` | — | Returns JSON with WiFi state, IP, heap, etc. |
| `GET_INFO` | — | Returns firmware version, chip info, partition |
| `WIFI_SCAN` | — | Scan for nearby networks |
| `WIFI_STATUS` | — | Current WiFi connection status |
| `WIFI_CONNECT` | `ssid,password` | Connect to a network |
| `WIFI_DISCONNECT` | — | Disconnect from WiFi |
| `WIFI_GET_SAVED` | — | Show saved credentials |
| `WIFI_FORGET` | — | Clear saved credentials |
| `VERBOSE` | `1` or `0` | Enable/disable verbose serial logging |
| `RESTART` | — | Reboot the device |
| `START_UPDATE` | `size,firmware\|filesystem` | Begin OTA update |
| `SEND_CHUNK` | base64 data | Send firmware chunk |
| `FINISH_UPDATE` | — | Finalize and reboot into new firmware |
| `ABORT_UPDATE` | — | Cancel in-progress update |

Response prefix: `OK:` (success) or `ERROR:` (failure), followed by a JSON body.

Example:
```
$ echo "GET_STATUS" > /dev/ttyACM0
OK:{"success":true,"state":"IDLE","wifi_connected":true,"ssid":"FP-BYOD","ip":"10.10.111.18",...}
```

## AP Credentials

| Field | Value |
|-------|-------|
| SSID | `BYTE90-Config` |
| Password | `12345678` |
| AP IP | `192.168.4.1` |
