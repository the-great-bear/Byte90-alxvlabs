# BYTE-90 Firmware — Architecture & Feature Breakdown

> Enterprise-architect-style overview of the BYTE-90 v3.0.0 (Series 2 AI Edition) firmware. This document complements the existing per-subsystem deep dives in [`docs/`](docs/) by giving a single, top-down view of the product, its modules, capabilities, integrations, and risks.
>
> **Scope:** the `main` branch of this repository (Xiaozhi AI / Tenclass pipeline). The `openai-api` branch swaps the AI backend but reuses the same architecture.
>
> **Audience:** maintainers, contributors, integrators, and reviewers evaluating the codebase.

---

## 1. Executive Summary

BYTE-90 is an **AI-enabled interactive desktop toy** built around a Seeed Studio XIAO ESP32-S3 (dual-core Xtensa LX7, 8 MB PSRAM, 8 MB flash). The firmware is a PlatformIO/Arduino C++ codebase that fuses six product pillars on a single MCU:

| Pillar | What it delivers |
|---|---|
| **Animated character** | 128×128 SSD1351 OLED running GIF-based emotes, retro CRT effects (scanlines, tints, glitch, dot matrix), and a digital clock mode. |
| **Motion-driven interactivity** | ADXL345 accelerometer detects taps, double-taps, shakes, tilts, and orientation changes — these drive animation state. |
| **Voice AI agent** | Full-duplex Opus audio over WebSocket/MQTT-UDP to the Xiaozhi (Tenclass) backend, with on-device STT/TTS streaming and emotion metadata. |
| **MCP tool framework** | The device exposes ~25 [Model Context Protocol](https://modelcontextprotocol.io/) tools (status, effects, clock, timers, weather, audio/display controls) callable by the LLM. |
| **Captive-portal configuration** | A Vite/TypeScript web UI flashed to LittleFS and served from the device for WiFi setup, brightness/volume/effects/themes, timezone/location, and OTA. |
| **Power management** | AXP2101 PMU with battery monitoring, progressive sleep modes, and DRV2605L haptic feedback for ~2 days of battery life. |

The architecture is intentionally layered: an **orchestrator entrypoint** (`src/main.cpp`) wires hardware and managers, **`src/Application*`** classes act as composition roots for audio / services / UI, and **feature modules** live as flat PlatformIO libraries under [`lib/`](lib/). Cross-cutting state is mediated by a lightweight `EventBus` and an explicit `SystemStateManager`.

The audio pipeline is the architectural centerpiece — a **dual-core, priority-scheduled, zero-churn FreeRTOS design** detailed in [`docs/MEMORY_AND_CORE_ARCHITECTURE.md`](docs/MEMORY_AND_CORE_ARCHITECTURE.md). Core 0 handles network + microphone capture/encode; Core 1 handles decode/resample/speaker output. A pre-allocated 4-slot PSRAM ring buffer eliminates `malloc`/`free` during streaming, and WiFi is given the highest task priority to protect connection stability.

---

## 2. Product Context

- **Hardware product:** ALXV Labs *BYTE-90 Series 2 AI Edition* (an upgrade-kit PCB for Series 2 devices; Series 1 is unsupported).
- **License:** Firmware is **GPL-3.0**. BYTE-90 branding, animations, 3D models, and visual identity are proprietary and **not in this repo**.
- **AI backend:** This branch targets **Xiaozhi AI** (free for personal dev, commercial license available). An `openai-api` branch swaps in the OpenAI Realtime API.
- **Distribution:** End users flash firmware via the official Web Serial portal at `install.alxv.dev/`; this repo is the source of truth that portal builds from.
- **Asset policy:** GIF animations are loaded from the LittleFS partition at runtime; DIY builders supply their own 128×128, 16 FPS, 8-bit indexed GIFs.

---

## 3. Hardware Platform

| Domain | Component | Module |
|---|---|---|
| MCU | XIAO ESP32-S3, 240 MHz, dual core, 8 MB OPI PSRAM | — |
| Flash | 8 MB; partition layout in [`partitions.csv`](partitions.csv) | — |
| Display | SSD1351 128×128 RGB OLED over SPI | [`lib/display/`](lib/display/), `ArduinoSSD1351` |
| Accelerometer | ADXL345 over I²C | [`lib/adxl/`](lib/adxl/) — `Adxl345`, `AdxlManager` |
| Power / battery | AXP2101 PMU over I²C, XPowersLib | [`lib/power/`](lib/power/) — `Axp2101` |
| Haptics | Adafruit DRV2605L (ERM actuator) over I²C | [`lib/haptics/`](lib/haptics/) — `HapticsManager` |
| Audio out | I²S DAC → speaker | [`lib/audio/AudioCodec`](lib/audio/) |
| Audio in | I²S microphone (PDM/MEMS) | [`lib/audio/AudioCodec`](lib/audio/) |
| RTC | RTClib-compatible RTC | [`lib/clock/ClockRtc`](lib/clock/) |
| I²C bus | Shared bus manager (singleton) | [`lib/i2c/I2CManager`](lib/i2c/) |

### Partition Layout

| Partition | Type | Offset | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data/nvs | 0x9000 | 16 KB | Persisted config (UUID, WiFi, theme, location, etc.) |
| `otadata` | data/ota | 0xd000 | 8 KB | OTA slot tracking |
| `phy_init` | data/phy | 0xf000 | 4 KB | RF calibration |
| `ota_0` | app | 0x10000 | 2.5 MB | Primary firmware slot |
| `ota_1` | app | 0x280000 | 2.5 MB | Failover firmware slot |
| `assets` | data/spiffs (LittleFS) | 0x500000 | 3 MB | GIFs, sounds, language packs, portal SPA |

> The 3 MB asset partition is new in v3.0.0 — that's why the install guide warns the first upgrade is a one-time partition-changing step requiring a `filesystem.bin` flash.

### Build Flags Worth Knowing

- `CORE_DEBUG_LEVEL=1` — INFO-level ESP logging
- `MQTT_MAX_PACKET_SIZE=2048` — sized for Tenclass MCP/MQTT control payloads
- `MBEDTLS_SSL_{IN,OUT}_CONTENT_LEN=4096` and `MBEDTLS_DYNAMIC_BUFFER=1` — TLS buffers right-sized and allocated from PSRAM via `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1`
- `CONFIG_SPIRAM_USE_MALLOC=1` — `malloc()` may fall back to PSRAM
- `lib_ldf_mode = deep+` — full transitive dependency resolution across `lib/`

---

## 4. Software Architecture

### 4.1 Layering Model

```
┌──────────────────────────────────────────────────────────────┐
│ src/main.cpp                       Composition / wiring root │
├──────────────────────────────────────────────────────────────┤
│ src/Application*.cpp               Application orchestration │
│   Application • ApplicationAudio • ApplicationServices       │
│   ApplicationUI • InputManager • ProtocolManager             │
│   ProvisioningManager • McpToolManager                       │
├──────────────────────────────────────────────────────────────┤
│ lib/system, lib/storage, lib/clock, lib/language             │
│                                       Cross-cutting services │
├──────────────────────────────────────────────────────────────┤
│ lib/ui, lib/effects, lib/gif_player Presentation             │
│ lib/audio, lib/network, lib/protocol,                        │
│ lib/services, lib/mcp                 Domain modules         │
├──────────────────────────────────────────────────────────────┤
│ lib/adxl, lib/display, lib/haptics, lib/i2c, lib/power       │
│                                              Drivers / HAL   │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 Boot Sequence (`src/main.cpp`)

`setup()` instantiates everything in a strict dependency order:

1. `TaskManager` (FreeRTOS task registry singleton)
2. `I2CManager` (shared bus)
3. `AXP2101` power manager (early — stable rails required)
4. `HapticsManager` (DRV2605L)
5. `Adxl345` accelerometer
6. `ArduinoSSD1351` display (applies stored brightness from NVS)
7. `NVSStorage` (loads/issues device UUID)
8. `LittleFSManager` (mounts asset partition)
9. `LanguageManager` (loads en-US / zh-CN pack)
10. `WifiManager` (captive portal configured)
11. `AdxlManager` (composes accel + haptics + display + WiFi + storage + power)
12. `SystemStateManager` (state machine; handles WiFi init + callbacks)
13. `SerialClient` (USB-CDC control plane)
14. `TenclassClient` (provisioning client)
15. `AudioCodec` (I²S in/out)
16. `Application` (composition root) → `initialize()` → `loop()`

`loop()` dispatches to `g_app->loop()`. Real-time work runs in dedicated FreeRTOS tasks; the Arduino loop is reserved for best-effort UI/input.

### 4.3 Key Patterns

- **Orchestrator + Managers** — `Application`, `ApplicationAudio`, `ApplicationServices`, `ApplicationUI` separate concerns; `*Manager` classes own their subordinate tasks and queues.
- **Adapter + Factory** — `ProtocolFactory` returns either a `TenclassWebsocketProtocol` or `TenclassMqttProtocol` against a shared `ProtocolClient` interface, with matching `TenclassWebsocketAudioSink` / `TenclassMqttAudioSink` for the audio plane.
- **Event Bus** — `lib/system/EventBus` decouples input/state producers from consumers (animations, UI, audio actions).
- **State Machine** — `SystemStateManager` and an explicit enum (`IDLE → CONNECTING → LISTENING → SPEAKING`) drive button semantics and animations.
- **Producer/Consumer queues + ring buffer** — audio uses FreeRTOS queues for control and a 4-slot PSRAM ring buffer for PCM handoff (semaphore-synchronized, zero malloc during streaming).
- **Memory pool segregation** — small / hot data in internal SRAM (stacks, queues, mutexes); large audio/TLS buffers in PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.
- **Capability accessors** — MCP tools reach subsystems through `McpServer` getters (`getEffectsManager()`, `getTimerManager()`, `getUi()`, …) so tool handlers stay decoupled from `Application`.

---

## 5. Module Inventory (`lib/`)

| Module | Files | Role |
|---|---|---|
| **`lib/adxl/`** | `Adxl345`, `AdxlManager` | ADXL345 driver + motion-event manager (taps, shakes, orientation, sleep wake). |
| **`lib/audio/`** | `AudioCodec`, `AudioService`, `AudioVisualizer`, `Mp3Decoder`, `Mp3Player`, `OpusDecoder`, `OpusEncoder`, `OpusResampler`, `ToneGenerator` | Full audio pipeline: I²S codec, Opus encode/decode + resample, MP3 playback for system sounds, tone generator, and on-screen audio visualizer. |
| **`lib/clock/`** | `ClockRtc`, `ClockSync` | RTC driver + NTP sync with timezone-name resolution. |
| **`lib/display/`** | `ArduinoSSD1351`, `CO5300/` | SSD1351 driver wrapper around Adafruit GFX; the `CO5300` folder reserves space for an alternate panel. |
| **`lib/effects/`** | `EffectsManager`, `RetroEffects`, `RetroTints` | Per-frame display post-processing (scanlines, dot matrix, glitch, green/blue/yellow tints). |
| **`lib/gif_player/`** | `GifManager`, `GifPlayer` | Bitbank2 `AnimatedGIF` integration; state-driven animation selection. |
| **`lib/haptics/`** | `HapticsManager` | DRV2605L ERM effects (clicks, ramps, audio-reactive haptics). |
| **`lib/i2c/`** | `I2CManager` | Shared I²C bus singleton (collision-free access for AXP2101, ADXL345, DRV2605L). |
| **`lib/language/`** | `LanguageManager` | Loads `data/lang/{en-US,zh-CN}/language.json` from LittleFS; locale-aware strings. |
| **`lib/mcp/`** | `McpServer`, `McpTool`, `McpToolRegistry`, `Property`, `PropertyList` | MCP server + tool registry + parameter schema primitives. Tools are registered in groups (status, effects, audio, timer, etc.) — see §8. |
| **`lib/network/`** | `WifiManager`, `CaptivePortal`, `MQTTClient`, `SecureHttpClient`, `SerialClient`, `WebsocketClient` | WiFi STA/AP, on-device HTTP captive portal, secure HTTPS, MQTT, WebSocket, and USB-CDC serial control. |
| **`lib/power/`** | `Axp2101` | AXP2101 PMU driver (battery voltage / charge state / IRQ). |
| **`lib/protocol/`** | `ProtocolClient`, `ProtocolFactory`, `TenclassMqttProtocol`, `TenclassMqttAudioSink`, `TenclassWebsocketProtocol`, `TenclassWebsocketAudioSink` | Protocol abstraction layer with factory and per-transport audio sinks. |
| **`lib/services/`** | `ApiClient`, `TenclassClient`, `TenclassMQTT`, `TenclassWebsocket` | High-level service clients: provisioning, MCP JSON payloads, MQTT/UDP and WebSocket session lifecycles. |
| **`lib/storage/`** | `LittlefsManager`, `NvsStorage` | LittleFS asset access + NVS-backed typed settings (system, locale, timezone, location, effects, theme, UUID, WiFi creds). |
| **`lib/system/`** | `DeviceConfig`, `DeviceSimulator`, `EventBus`, `SystemState`, `TaskManager`, `TimerManager`, `certs/` | Pin map / build-time config, host-side simulator stub, pub/sub event bus, system-state machine, FreeRTOS task registry, single-shot timer manager, and bundled root CA certificates. |
| **`lib/ui/`** | `ChargingAnimator`, `DigitalClock`, `DigitalClockController`, `DosBootAnimator`, `HapticsVisualizer`, `MotionAnimator`, `SleepAnimator`, `StatusBarLayout`, `TypingEffect`, `UIVisualizer` | UI orchestration: boot animation, digital clock mode, charging/sleep visuals, motion overlays, typing/teletype effect, and shared status bar layout. |

### `src/` (Application Layer)

| File | Responsibility |
|---|---|
| `main.cpp` | Hardware/manager wiring; `setup()` + `loop()` only. |
| `Application.{h,cpp}` | Composition root; holds all manager references; drives boot sequence and main loop. |
| `ApplicationAudio.{h,cpp}` | Audio pipeline owner; coordinates capture/playback tasks; TTS watchdog (30 s timeout). |
| `ApplicationServices.{h,cpp}` | Routes EventBus actions to protocol clients and MCP tool execution. |
| `ApplicationUI.{h,cpp}` | Binds system state to display/GIF/UI components. |
| `InputManager.{h,cpp}` | Button decision tree (single-click, long-press, state-aware). |
| `ProtocolManager.{h,cpp}` | Lifecycle of the active protocol (connect / disconnect / reconnect / failover). |
| `ProvisioningManager.{h,cpp}` | First-boot activation flow against `api.tenclass.net/xiaozhi/ota/`. |
| `McpToolManager.{h,cpp}` | Owns the `McpServer` lifecycle and tool registration. |

### `include/`

- `StartupImage.h` — built-in boot splash bitmap.

---

## 6. Feature Areas

### 6.1 Display & Animation

- **GIF playback** via Bitbank2 `AnimatedGIF` (`lib/gif_player/`), driven by `GifManager` from system state and motion events.
- **Retro effects pipeline** (`lib/effects/`): scanlines, dot matrix, glitch; tint colors green / blue / yellow (red is *not* implemented — see [`docs/LOCAL_HTTP_API.md`](docs/LOCAL_HTTP_API.md)).
- **UI overlays** (`lib/ui/`): DOS-style boot animation, charging animator, sleep animator, motion animator, status bar, digital clock controller, and typing/teletype effect for in-character speech.
- **Resolution / format**: 128×128, 8-bit indexed GIF (≤256 colors), 16 FPS recommended.

### 6.2 Audio Pipeline

> Authoritative deep dives: [`docs/AUDIO_PROCESSING.md`](docs/AUDIO_PROCESSING.md), [`docs/AUDIO_STREAMING.md`](docs/AUDIO_STREAMING.md), [`docs/MEMORY_AND_CORE_ARCHITECTURE.md`](docs/MEMORY_AND_CORE_ARCHITECTURE.md).

- **I²S full duplex** through `AudioCodec` (separate mic + speaker pinouts).
- **Opus** encode (16 kHz STT) and decode (24 kHz TTS) at 60 ms frames via `sh123/esp32_opus`; `OpusResampler` handles rate conversion when needed.
- **MP3 playback** (`Mp3Player`, `Mp3Decoder`) for bundled system cues in `data/sounds/` (startup-95/xp/mac, charging, dizzy, online, etc.).
- **Tone generator** (`ToneGenerator`) for procedural beeps/alerts.
- **Audio visualizer** (`AudioVisualizer` + `HapticsVisualizer`) maps audio energy to display effects and haptics.
- **Dual-core layout**: Core 0 = WiFi + capture + encode (priority 5); Core 1 = decode (priority 2) + output (priority 5) + main loop (priority 1). WiFi runs at priority ~23 and can preempt everything.
- **Zero-churn buffers**: 4-slot PSRAM ring buffer (225 KB) for PCM handoff between decode and output; no `malloc`/`free` during streaming.
- **Fail-safe**: non-blocking `xQueueSend` drops a packet rather than blocking and tripping the watchdog when the network falls behind.

### 6.3 Motion & Input

- ADXL345 → `AdxlManager` detects taps, double-taps, shakes, tilts, and orientation changes.
- Motion events drive animation state selection (e.g., dizzy animation after shake) and wake the device from sleep.
- Button input is routed through `InputManager`, which uses `SystemStateManager` to interpret a click context-sensitively (start/stop listening, abort TTS, etc.) — see `docs/BUTTON_INTERACTION.md`.

### 6.4 Power Management

- **AXP2101** monitors battery voltage and charge state.
- **Progressive sleep modes** managed by `AdxlManager` + `SystemStateManager` (`LIGHT_SLEEP_INTERVAL_MS`).
- **Charging animation** (`ChargingAnimator`) and **shutdown sounds** (`shutdown-95.mp3`, `shutdown-xp.mp3`).
- Vendor lib: `lewisxhe/XPowersLib`.
- Vendor claim: up to **2 days** on battery.

### 6.5 Storage

- **NVS** via `NvsStorage` for typed settings: device UUID (generated on first boot), WiFi creds, system settings (brightness, volume, theme), language, timezone, location, effects flags.
- **LittleFS** (3 MB partition) for runtime assets: GIFs, MP3 sounds, language packs, captive portal SPA.

### 6.6 Configuration & Language

- `LanguageManager` loads `data/lang/<locale>/language.json` (currently `en-US`, `zh-CN`).
- `data/lang/README.md` documents the schema. See `docs/LANGUAGE_SYSTEM.md` for the loading flow.
- `docs/EMOJI_SYSTEM.md` describes the emotion → emoji/animation mapping the LLM uses (`llm` messages carry an `emotion` field — see API_REFERENCE §19).

### 6.7 Clock & Timers

- `ClockSync` syncs via NTP and resolves IANA timezone names.
- `ClockRtc` reads/writes the on-board RTC.
- `DigitalClock` + `DigitalClockController` render full-screen clock mode (enabled by the `self.display.show_clock` MCP tool).
- `TimerManager` (`lib/system/`) is the engine behind the LLM-callable timer tools (1 s – 8 h range; mutually-exclusive hours/minutes/seconds).

---

## 7. AI Integration & Protocol

> Authoritative deep dives: [`docs/XIAOZHI_PROVISIONING_API.md`](docs/XIAOZHI_PROVISIONING_API.md), [`docs/XIAOZHI_WEBSOCKET_API_FLOW.md`](docs/XIAOZHI_WEBSOCKET_API_FLOW.md), [`docs/XIAOZHI_MQTTUDP_API_FLOW.md`](docs/XIAOZHI_MQTTUDP_API_FLOW.md), [`docs/XIAOZHI_MCPTOOL_API.md`](docs/XIAOZHI_MCPTOOL_API.md), [`docs/XIAOZHI_ROLE.md`](docs/XIAOZHI_ROLE.md), [`docs/API_REFERENCE.md`](docs/API_REFERENCE.md).

### 7.1 Provisioning

- On boot, the device POSTs hardware/firmware metadata (chip info, MAC, UUID, free heap, board, current firmware version) to `https://api.tenclass.net/xiaozhi/ota/`.
- The response carries MQTT credentials, WebSocket URL+token, server time/timezone, and any firmware OTA URL.

### 7.2 Transports

Both transports are produced by `ProtocolFactory` and share a common control-message vocabulary:

| Transport | Control plane | Audio plane | Audio crypto |
|---|---|---|---|
| **WebSocket** (`TenclassWebsocketProtocol`) | `wss://api.tenclass.net/xiaozhi/v1/` | Same WebSocket | TLS |
| **MQTT + UDP** (`TenclassMqttProtocol`) | MQTT topics (`device-server` + per-device p2p) | UDP datagrams | **AES-128-CTR** with server-issued key + nonce |

### 7.3 Message Vocabulary

`hello` → `listen` (`start`/`detect`/`stop`) → `stt` (recognized text) → `llm` (response + emotion) → `tts` (`start` → `sentence_start` → `sentence_end` → `stop`). `abort` cancels. `mcp` carries JSON-RPC 2.0 payloads for tool discovery/execution. Hello announces `features.mcp = true` and `features.aec = true`.

### 7.4 Model Context Protocol Server

The firmware implements MCP `protocolVersion: 2024-11-05` with server name `BYTE-90`, version `3.0.0`. Tool discovery is paginated. The server consumes vision capabilities advertised by the client (`api.xiaozhi.me/vision/explain`).

---

## 8. MCP Tools Catalog

All tools are registered in [`lib/mcp/McpToolRegistry.cpp`](lib/mcp/McpToolRegistry.cpp) under namespace `self.*`. Patterns documented in [`docs/MCP_TOOLS_GUIDE.md`](docs/MCP_TOOLS_GUIDE.md).

### Status & Identity
| Tool | Purpose |
|---|---|
| `self.describe_self` | Persona / self-description payload. |
| `self.tell_joke` | LLM guidance: 3 retro-themed jokes. |
| `self.get_device_status` | Aggregated snapshot of audio, display, effects, locale, network, websocket, timer. **Recommended pre-flight before mutating state.** |

### Time & Location
| Tool | Purpose |
|---|---|
| `self.get_time` | Returns NTP-synced time for a timezone. |
| `self.set_timezone` | Persist user timezone to NVS. |
| `self.set_location` | Persist user location to NVS (for weather). |
| `self.get_weather` | Google Geocoding + Weather API; falls back to stored location; units `c`/`f`. **Requires `GOOGLE_WEATHER_API_KEY` to be set at build time.** |

### Audio & Display
| Tool | Purpose |
|---|---|
| `self.audio_speaker.set_volume` | Integer 0–100. |
| `self.display.set_brightness` | Integer 0–100. |
| `self.display.show_clock` | Enter clock mode; validates and applies timezone. |
| `self.display.stop_clock` | Exit clock mode; resume GIFs. |

### Display Effects
| Tool | Purpose |
|---|---|
| `self.display_effects.enable_scanlines` / `disable_scanlines` | Scanline overlay. |
| `self.display_effects.enable_glitch` / `disable_glitch` | Glitch effect. |
| `self.display_effects.enable_dot_matrix` / `disable_dot_matrix` | Dot-matrix overlay. |
| `self.display_effects.enable_tint_{green,blue,yellow}` | Mutually exclusive tints. |
| `self.display_effects.disable_tint` | Clear active tint. |
| `self.display_effects.disable_all` | Reset all effects + tints. |

### Timers
| Tool | Purpose |
|---|---|
| `self.timer.set` | One of `hours` (0–8), `minutes` (0–480), `seconds` (0–28800). Returns `ends_at_epoch_ms`. |
| `self.timer.status` | Active state + remaining time. |
| `self.timer.cancel` | Cancel active timer. |
| `self.timer.repeat` | Restart most recent duration. |

> Tools return one of: `ReturnValue(true/false)`, an explicit JSON error string, or a heap-owned `JsonDocument*` for structured success. The tool framework supports `Property` parameters with min/max validation.

---

## 9. Networking & Local APIs

### 9.1 Captive Portal & HTTP

When the device boots without saved WiFi credentials, `WifiManager` runs an AP (`BYTE90-Config` / password `12345678`) with the captive portal at `192.168.4.1`. Once connected, the same HTTP server is also reachable on the station IP.

Active endpoints in shipped 3.1.0 binary:

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/status` | WiFi connection status |
| `GET` | `/api/timezone/list` | Available timezones |

Registered in `CaptivePortal::setupRoutes()` (becoming active in upcoming firmware — see [`docs/LOCAL_HTTP_API.md`](docs/LOCAL_HTTP_API.md)): effects (`/api/effects[/status]`), audio (`/api/audio[/status,/reset]`), clock (`/api/clock[/status]`), timezone/location getters/setters, WiFi scan / configure / disconnect.

### 9.2 USB-CDC Serial Control

`SerialClient` exposes a text protocol at **921 600 baud** on `/dev/ttyACM0` for use by the Web Serial portal. Commands include `GET_STATUS`, `GET_INFO`, `WIFI_SCAN`, `WIFI_CONNECT:ssid,password`, `START_UPDATE:size,firmware|filesystem`, `SEND_CHUNK:<base64>`, `FINISH_UPDATE`, `RESTART`, etc. Responses are prefixed `OK:` or `ERROR:` followed by JSON.

### 9.3 OTA Update Paths

1. **Web Serial portal** (`install.alxv.dev`) — chunked base64 over USB-CDC; updates either `ota_0/ota_1` (firmware) or `assets` (filesystem image).
2. **Server-initiated** — provisioning response can carry `firmware.url` that the device fetches via `SecureHttpClient`.

---

## 10. Web Portal Frontend (`webserver/`)

- **Stack:** Vite + TypeScript (no framework) with ESLint + tsconfig split (`app` / `node`).
- **Entry:** `webserver/index.html` + `webserver/src/main.ts` + `webserver/src/portal.css`.
- **Assets:** 24 inline SVG icons under `webserver/src/assets/` (WiFi signal levels, device-info, effects, brightness/volume, firmware, etc.).
- **Build output** is committed to [`data/portal/`](data/portal/) (`index.html`, `index-*.js`, `index-*.css`) so it ships as part of the LittleFS image flashed at install time.
- **Runtime:** Served by `CaptivePortal` from LittleFS; talks to the local HTTP API in §9.1.

---

## 11. Bundled Assets (`data/`)

| Folder | Contents |
|---|---|
| `data/lang/` | `en-US/language.json`, `zh-CN/language.json` (i18n string packs). |
| `data/sounds/` | 18 MP3 cues: startup-95/mac/xp, shutdown-95/xp, charging, confused, connecting, crash, disconnect, dizzy, interrupt, online, speaking, tap, teams, timer. |
| `data/portal/` | Built captive portal SPA (HTML/JS/CSS). |

No GIFs ship with the open-source firmware (animations are proprietary; DIY builders supply their own).

---

## 12. Build, Test, and Deploy

### Build / Flash (developers)

```bash
pio run -e seeed_xiao_esp32s3                  # build firmware
pio run -e seeed_xiao_esp32s3 -t upload        # flash firmware
pio run -e seeed_xiao_esp32s3 -t uploadfs      # flash LittleFS image
pio device monitor -b 115200                   # serial monitor
```

Per [`AGENTS.md`](AGENTS.md): contributors should **not** run `pio run` / `upload` in automation — the maintainer builds and flashes locally.

### Test

- **Framework:** PlatformIO + Unity (`test_framework = unity`, `[env:seeed_xiao_esp32s3_test]`).
- **Suites** under [`test/`](test/): `button_state`, `event_bus`, `input_manager`, `mcp_tool_manager`, `protocol_manager`, `storage_language`, `system_state`, `task_manager`, `timer_manager`, plus `test_common.*` and `test_runner.cpp`.
- Tests are skipped in the production env (`test_ignore = *`) and compiled with `-DTEST_ENV` + USB-CDC-on-boot flags in the dedicated test env.

### Tooling

- [`tools/doc_lint.py`](tools/doc_lint.py) — documentation linter.
- [`.vscode/`](.vscode/) — recommended VS Code config.
- `compile_commands.json` is committed (6.5 MB) to support clangd / IDEs without a fresh PlatformIO build.

### Release Artifacts

- [`firmware-backups/`](firmware-backups/) holds checkpoint binaries (recent commits include the original maker firmware and pre-update / post-update snapshots from June 2026), useful for rollback during destructive partition migrations.

### Coding Standards

Per [`docs/CODING_STYLE_GUIDE.md`](docs/CODING_STYLE_GUIDE.md) and `AGENTS.md`:

- 4-space indent, K&R braces, `#pragma once` headers, lines ≤120 chars.
- Files/classes `PascalCase`; methods `camelCase`; locals `snake_case`; members `_snake_case`.
- Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`).

---

## 13. External Dependencies

| Library | Version | Role |
|---|---|---|
| `lewisxhe/XPowersLib` | ^0.3.1 | AXP2101 power management |
| `bblanchon/ArduinoJson` | ^7.4.2 | JSON serialization (API, config, MCP payloads) |
| `sh123/esp32_opus` | ^1.0.3 | Opus codec |
| `adafruit/Adafruit GFX Library` | ^1.11.11 | 2D graphics |
| `adafruit/Adafruit SSD1351 library` | ^1.3.2 | OLED display driver |
| `adafruit/Adafruit DRV2605 Library` | ^1.2.4 | Haptic driver |
| `adafruit/Adafruit ADXL345` | ^1.3.4 | Accelerometer driver |
| `adafruit/RTClib` | ^2.1.4 | Real-time clock |
| `bitbank2/AnimatedGIF` | ^2.2.0 | GIF decoding |
| `arduino-libraries/ArduinoMqttClient` | ^0.1.8 | MQTT transport |

Bundled trust roots live under `lib/system/certs/` for TLS against Tenclass and OTA endpoints.

---

## 14. Risks & Observations

An enterprise-architect read of the codebase surfaces the following items worth tracking — none are blockers, but each is the kind of thing a downstream integrator should know.

| # | Area | Observation | Suggested mitigation |
|---|---|---|---|
| 1 | **Captive portal credentials** | AP SSID/password are static (`BYTE90-Config` / `12345678`) and documented publicly. Anyone in RF range can join the AP during provisioning. | Acceptable for short-lived provisioning windows; consider a per-device suffix and brief AP timeout. |
| 2 | **No on-portal authentication** | Active local HTTP endpoints (`/api/status`, `/api/timezone/list`) are unauthenticated; the route table being unlocked in subsequent releases will widen the exposed surface to volume, brightness, WiFi, and tints. | Once mutating endpoints are enabled, consider a portal-only session token or restrict mutating routes to the AP interface. |
| 3 | **Hardcoded MCP cloud endpoints** | Tenclass URLs (`api.tenclass.net`, `mqtt.xiaozhi.me`) are baked into the firmware. | Already addressed by the `openai-api` branch model; consider a build-time `lib_deps`-style configuration to swap backends without code edits. |
| 4 | **`GOOGLE_WEATHER_API_KEY`** | The `get_weather` tool depends on a build-time key. If unset, the tool returns a structured error — but key handling is a config concern, not a code one. | Document key provisioning in `README.md` and prefer NVS-stored keys over compile-time secrets for self-hosters. |
| 5 | **Single-language `zh-CN` parity** | Only `en-US` and `zh-CN` packs exist under `data/lang/`. Any new UI text needs to be added in both. | Lint that gates merges on key-parity across locale packs. |
| 6 | **Wake-word "synthetic detect"** | API_REFERENCE §16 notes real wake-word detection is not implemented; a synthetic `listen.detect` message prompts greetings. | Document the user-facing implication (button-driven, not voice-driven) clearly outside the developer docs. |
| 7 | **TLS heap pressure** | `MBEDTLS_DYNAMIC_BUFFER` + `MBEDTLS_EXTERNAL_MEM_ALLOC` push TLS buffers into PSRAM. The architecture is sound, but the `OpenAI Realtime` build doubles PSRAM usage (~2 MB of ring buffers). | Memory dashboard during long-running OpenAI sessions; current monitoring covers send-queue fill but not PSRAM headroom. |
| 8 | **`compile_commands.json` (6.5 MB) committed** | Convenient for IDEs but inflates clone size and churns on flag changes. | Move to `.gitignore` and document the `pio run -t compiledb` workflow. |
| 9 | **`webserver/node_modules` build artifact discipline** | The portal is rebuilt and the output committed to `data/portal/`. There is no CI gate ensuring `data/portal/` matches `webserver/src/`. | Add a CI step that re-runs `vite build` and diffs `data/portal/`. |
| 10 | **No `.github/` workflows** | The repo has no committed CI configuration — linting, build, and test all rely on maintainer discipline. | Even a single `pio run` matrix would catch regressions before maintainer flash. |
| 11 | **Documentation versioning drift** | [`docs/LOCAL_HTTP_API.md`](docs/LOCAL_HTTP_API.md) references firmware "3.1.0" while [`platformio.ini`](platformio.ini) declares `3.0.0`. | Single-source the version (e.g., generate doc snippets from `platformio.ini`). |
| 12 | **License vs branding clarity** | GPL-3.0 firmware but proprietary branding/assets is correctly called out in `README.md` and `CONTRIBUTING.md`. Downstream forks should preserve both notices verbatim. | No action; flagged for awareness. |

---

## 15. Where to Read Next

- High-level boot + state flow → [`docs/PROJECT_STRUCTURE_OVERVIEW.md`](docs/PROJECT_STRUCTURE_OVERVIEW.md)
- Audio architecture / memory model → [`docs/MEMORY_AND_CORE_ARCHITECTURE.md`](docs/MEMORY_AND_CORE_ARCHITECTURE.md), [`docs/AUDIO_PROCESSING.md`](docs/AUDIO_PROCESSING.md), [`docs/AUDIO_STREAMING.md`](docs/AUDIO_STREAMING.md)
- AI / protocol flows → `docs/XIAOZHI_*` family (provisioning, websocket, mqtt-udp, mcptool, role), [`docs/API_REFERENCE.md`](docs/API_REFERENCE.md)
- Adding MCP tools → [`docs/MCP_TOOLS_GUIDE.md`](docs/MCP_TOOLS_GUIDE.md)
- Local control plane → [`docs/LOCAL_HTTP_API.md`](docs/LOCAL_HTTP_API.md)
- Coding conventions → [`docs/CODING_STYLE_GUIDE.md`](docs/CODING_STYLE_GUIDE.md), [`AGENTS.md`](AGENTS.md)
- Linux dev workflow → [`docs/LINUX_DEV_SETUP.md`](docs/LINUX_DEV_SETUP.md)
