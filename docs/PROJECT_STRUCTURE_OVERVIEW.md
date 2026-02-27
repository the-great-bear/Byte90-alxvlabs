# PROJECT_STRUCTURE_OVERVIEW

Design pattern and module organization outline for the current codebase.

---

## 1. Overview

- **Entry point:** `src/main.cpp` wires hardware and top-level services.
- **Application layer:** `src/` holds `Application*` classes and core managers.
- **Modules:** Feature domains live in `lib/` as PlatformIO libraries.
- **Assets:** Runtime assets under `data/` (LittleFS).
- **Web portal:** `webserver/` contains the captive portal UI (Vite build).
- **Docs:** Design notes under `docs/`.

---

## 2. Layering Model (High-Level)

- **Application orchestration:** `src/main.cpp`, `src/Application*.cpp`, `src/*Manager.cpp`
- **Feature modules:** `lib/*` (each folder is a library)
- **Drivers/HAL:** hardware-specific modules under `lib/`
- **Services/Protocol:** network, protocol, and control-plane logic under `lib/`
- **Portal UI:** `webserver/` (frontend only; served by captive portal)

---

## 3. Module Categories

### 3.1 Hardware & Drivers

- `lib/adxl/` - motion sensor stack
- `lib/display/` - display hardware drivers
- `lib/haptics/` - haptics driver
- `lib/i2c/` - I2C bus management
- `lib/power/` - power/AXP2101 management

### 3.2 Audio & Media

- `lib/audio/` - capture, encode/decode, playback, audio routing
- `lib/gif_player/` - GIF playback stack

### 3.3 UI & Effects

- `lib/ui/` - UI orchestration and visual state
- `lib/effects/` - display effects (tints, scanlines, dot matrix, glitch)

### 3.4 System & Storage

- `lib/system/` - state, tasks, event bus
- `lib/storage/` - NVS/LittleFS storage
- `lib/clock/` - time sync utilities
- `lib/language/` - localization

### 3.5 Networking & Protocol

- `lib/network/` - WiFi, captive portal, HTTP endpoints
- `lib/protocol/` - protocol abstractions/adapters
- `lib/services/` - protocol-facing service clients (Tenclass HTTP/MQTT/WebSocket, ApiClient)
- `lib/mcp/` - MCP tool registry and execution

---

## 4. Design Patterns Used

- **Orchestrator entry point:** `src/main.cpp` wires dependencies.
- **Adapter pattern:** protocol adapters in `lib/protocol/`.
- **Factory pattern:** `ProtocolFactory` centralizes protocol creation.
- **Event bus:** `EventBus` decouples inputs from actions.
- **Manager pattern:** `ProtocolManager`, `ProvisioningManager`, `McpToolManager`.

---

## 5. Ownership & Dependencies

- **`src/main.cpp` owns instantiation** of hardware and core services.
- **Managers own their internal workers** (queues/tasks) and clean them up.
- **Modules avoid cross-domain includes**; use interfaces or managers.
- **Protocol selection is centralized** in `ProtocolFactory`.

---

## 6. PlatformIO Alignment

- `src/` contains the entry point and application layer.
- `lib/` contains project libraries (flat layout; no `src/` subfolders).
- `include/` contains project-level headers (currently `StartupImage.h`).

---

## 7. Key Application Files

- `src/Application.cpp`, `src/Application.h` - Composition root; wires hardware, managers, and UI; runs main loop and startup sequence.
- `src/ApplicationAudio.cpp`, `src/ApplicationAudio.h` - Audio pipeline owner; coordinates capture/playback and audio tasks.
- `src/ApplicationServices.cpp`, `src/ApplicationServices.h` - Service router; connects EventBus actions to protocol and MCP.
- `src/ApplicationUI.cpp`, `src/ApplicationUI.h` - UI orchestration; ties system state to display/GIF playback.
- `src/InputManager.cpp`, `src/InputManager.h` - Input and button decision logic.
- `src/ProtocolManager.cpp`, `src/ProtocolManager.h` - Protocol lifecycle coordinator.
- `src/ProvisioningManager.cpp`, `src/ProvisioningManager.h` - Provisioning workflow owner.
- `src/McpToolManager.cpp`, `src/McpToolManager.h` - MCP tool execution owner.

---

## 8. Portal (Web UI)

- `webserver/` contains the captive portal frontend (Vite).
- `webserver/index.html` is the entry template.
- `webserver/src/` contains the TypeScript and styles that build into `/portal/*` assets.

---

## 9. Extension Rules (Guidelines)

- Add a new module as a new folder under `lib/`.
- Keep module headers local unless they are project-wide.
- Prefer adapters + factories over `#if` branching in application code.
- Keep hardware drivers isolated from higher-level services.
