# MCP Tools Guide (Examples: Tell Joke, Enable Effect, Get Weather)

This guide documents how to create MCP tools in `lib/mcp/McpToolRegistry.cpp` using distinct examples:
- `self.tell_joke`
- `self.display_effects.enable_scanlines` (example effect tool)
- `self.get_weather`
- `self.get_device_status`
- `self.display.show_clock`
- `self.set_location` (validated setter + persist)
- `self.timer.set` (timer with constraints)

The goal is to show the common structure, how to define parameters, how to return data, and how to handle errors.

## Where MCP Tools Live

All device MCP tools are registered in:
- `lib/mcp/McpToolRegistry.cpp`

Tools are grouped by area and registered via helpers like `registerStatusTools()`, `registerEffectsTools()`, and `registerAudioTools()`.

## Anatomy of a Tool

Each tool is registered with `mcp->addTool(...)` and has four parts:
1. Tool name (string)
2. Tool description (string)
3. Parameter schema (`PropertyList`)
4. Handler lambda: `[](PropertyList& params) -> ReturnValue { ... }`

### Skeleton

```cpp
mcp->addTool(
    "self.example.tool",
    "Human-readable description with usage notes.",
    PropertyList({
        Property("param", PROPERTY_TYPE_STRING, "default")
    }),
    [mcp](PropertyList& params) -> ReturnValue {
        // 1) Validate params
        // 2) Access system components via mcp
        // 3) Execute behavior
        // 4) Return ReturnValue(true) or JSON
    }
);
```

## Example 1: `self.tell_joke` (No Params, Returns JSON)

This tool shows a minimal pattern with no parameters and a JSON response payload.

Key ideas:
- Empty `PropertyList()` for no params.
- Return `JsonDocument*` for structured data.
- No device state needed.

```cpp
mcp->addTool(
    "self.tell_joke",
    "Guide the assistant to tell 3 jokes themed around retro computing, development, Byte 90, retro gaming, and microcontrollers.\n"
    "Use this tool for:\n"
    "1. When the user asks for jokes or a funny response.",
    PropertyList(),
    [](PropertyList& params) -> ReturnValue {
        (void)params;
        JsonDocument* response = new JsonDocument();
        (*response)["style"] = "Tell 3 short, friendly jokes back-to-back. Pause briefly between each joke and make snarky comments for comedic timing.";
        (*response)["themes"] = "retro computing, development, Byte 90, retro gaming, microcontrollers";
        return ReturnValue(response);
    }
);
```

Pattern takeaway:
- Use this for LLM guidance tools that don’t touch hardware.

## Example 2: `self.display_effects.enable_scanlines` (No Params, Writes State)

This tool toggles a display effect and persists it to storage.

Key ideas:
- Access subsystem via `mcp->getEffectsManager()`.
- Update storage if available.
- Return `ReturnValue(true)` for success.
- Validate the subsystem pointer before use.

```cpp
mcp->addTool(
    "self.display_effects.enable_scanlines",
    "Enable scanline effects on the display (retro CRT look).\n"
    "Use this tool for:\n"
    "1. Turning scanlines on when the user asks for scanline effect.\n"
    "2. Applying scanlines as part of a retro visual request.\n"
    "Notes: call self.get_device_status first to confirm current effects state.",
    PropertyList(),
    [mcp](PropertyList& params) -> ReturnValue {
        (void)params;
        auto* effects = mcp->getEffectsManager();
        auto* storage = mcp->getStorage();
        if (!effects) {
            ESP_LOGE(TAG, "[MCP] Effects manager not available");
            return ReturnValue(false);
        }
        effects->setScanlinesEnabled(true);
        if (storage) {
            storage->setEffectsScanlinesEnabled(true);
        }
        ESP_LOGI(TAG, "[MCP] Scanlines enabled");
        return ReturnValue(true);
    }
);
```

Pattern takeaway:
- Use `mcp` to access device subsystems.
- Persist user-facing settings when applicable.

## Example 3: `self.get_weather` (Params, External API, JSON Response)

This tool demonstrates richer validation, external HTTP calls, and structured results.

Key ideas:
- Parameters with defaults and validation.
- Fallback to stored location if not provided.
- Return JSON errors as strings for predictable responses.
- Cache by session to avoid repeat calls.

```cpp
mcp->addTool(
    "self.get_weather",
    "Fetch current weather for a location using Google Geocoding and Weather APIs.\n"
    "Use this tool for:\n"
    "1. Answering current weather questions.\n"
    "2. Fetching temperature, conditions, humidity, and wind details.\n"
    "Notes: provide location or set it with self.set_location. Units are c/f.",
    PropertyList({
        Property("location", PROPERTY_TYPE_STRING, ""),
        Property("unit", PROPERTY_TYPE_STRING, "c")
    }),
    [mcp](PropertyList& params) -> ReturnValue {
        String location = params["location"].getStringValue();
        String unit = params["unit"].getStringValue();
        unit.toLowerCase();
        location.trim();
        if (location.isEmpty() && mcp && mcp->getStorage()) {
            location = mcp->getStorage()->getLocation();
            location.trim();
        }

        if (GOOGLE_WEATHER_API_KEY[0] == '\0') {
            return ReturnValue("{\"error\":\"missing GOOGLE_WEATHER_API_KEY\"}");
        }

        if (unit != "c" && unit != "f") {
            return ReturnValue("{\"error\":\"unit must be 'c' or 'f'\"}");
        }

        if (location.isEmpty()) {
            return ReturnValue("{\"error\":\"location_required\",\"message\":\"Provide location or set it with self.set_location.\"}");
        }

        // HTTP requests, parse JSON, build response document...
        JsonDocument* response = new JsonDocument();
        JsonObject weather = (*response)["weather"].to<JsonObject>();
        weather["location"] = location;
        weather["unit"] = unit;
        return ReturnValue(response);
    }
);
```

Pattern takeaway:
- Validate parameters early.
- Return explicit error JSON strings for consistent failures.
- Use `JsonDocument*` for structured success responses.

## Example 4: `self.get_device_status` (Aggregated Status Snapshot)

This tool gathers multiple subsystems into a single response payload.

Key ideas:
- Useful as a "preflight" tool before changing settings.
- Builds nested JSON objects for `audio`, `display`, `effects`, `locale`, `network`, `websocket`, and `timer`.
- Must guard subsystem pointers and fill sane defaults.

```cpp
mcp->addTool(
    "self.get_device_status",
    "Provide real-time device status including audio, display, network, and timer info.\n"
    "Use this tool for:\n"
    "1. Answering questions about current device state (volume, brightness, network).\n"
    "2. As a first step before changing device settings.",
    PropertyList(),
    [mcp](PropertyList& params) -> ReturnValue {
        (void)params;
        JsonDocument* status = new JsonDocument();
        JsonObject audio = (*status)["audio"].to<JsonObject>();
        JsonObject disp = (*status)["display"].to<JsonObject>();
        JsonObject effects_status = (*status)["effects"].to<JsonObject>();
        JsonObject locale = (*status)["locale"].to<JsonObject>();
        JsonObject network = (*status)["network"].to<JsonObject>();
        JsonObject ws = (*status)["websocket"].to<JsonObject>();
        JsonObject timer = (*status)["timer"].to<JsonObject>();
        // Fill values with subsystem checks...
        return ReturnValue(status);
    }
);
```

Pattern takeaway:
- Use when a response depends on multiple subsystems or when you want a single status snapshot.

## Example 5: `self.display.show_clock` (UI Side Effects + Validation)

This tool validates timezone, syncs time, and enters UI clock mode.

Key ideas:
- Accepts optional param, falls back to storage.
- Validates timezone with `ClockSync` before applying.
- Returns structured JSON for success or explicit error JSON.

```cpp
mcp->addTool(
    "self.display.show_clock",
    "Display a digital clock on screen using the provided timezone and read out the current time.\n"
    "Use this tool for:\n"
    "1. Entering clock mode when the user asks to show the clock/time on screen.\n"
    "2. Showing the time in a specific timezone (set or provided).\n"
    "Notes: call self.get_device_status first to confirm whether clock mode is already active.",
    PropertyList({
        Property("timezone_name", PROPERTY_TYPE_STRING, "")
    }),
    [mcp](PropertyList& params) -> ReturnValue {
        String timezone_name = params["timezone_name"].getStringValue();
        timezone_name.trim();
        if (timezone_name.isEmpty() && mcp && mcp->getStorage()) {
            timezone_name = mcp->getStorage()->getTimezoneName();
            timezone_name.trim();
        }
        if (timezone_name.isEmpty()) {
            return ReturnValue("{\"error\":\"timezone_required\",\"message\":\"Provide timezone_name or set it with self.set_timezone.\"}");
        }

        ClockSync clock_sync;
        if (!clock_sync.setTimezoneByName(timezone_name.c_str())) {
            return ReturnValue("{\"error\":\"timezone_invalid\",\"message\":\"Unknown timezone_name.\"}");
        }

        auto* ui = mcp->getUi();
        if (!ui) {
            return ReturnValue("{\"error\":\"ui_unavailable\",\"message\":\"UI not available.\"}");
        }

        if (!ui->showClock(timezone_name)) {
            return ReturnValue("{\"error\":\"clock_failed\",\"message\":\"Unable to show clock.\"}");
        }

        String result = "{\"status\":\"ok\",\"timezone_name\":\"" + timezone_name + "\"}";
        return ReturnValue(result);
    }
);
```

Pattern takeaway:
- Use when you need input validation + side effects + a clear success payload.

## Example 6: `self.set_location` (Validated Setter + Persist)

These tools validate a value and persist it to NVS.

Key ideas:
- Validate required string.
- Return structured error JSON when missing.
- Save to storage and return a small `{ status: "ok" }` JSON.

```cpp
mcp->addTool(
    "self.set_location",
    "Set the default location for MCP tools and persist it to storage.\n"
    "Use this tool for:\n"
    "1. Saving a user's default location for weather queries.",
    PropertyList({
        Property("location", PROPERTY_TYPE_STRING)
    }),
    [mcp](PropertyList& params) -> ReturnValue {
        String location = params["location"].getStringValue();
        location.trim();
        if (location.isEmpty()) {
            return ReturnValue("{\"error\":\"location_required\",\"message\":\"Provide a location (e.g., Toronto, CA).\"}");
        }
        auto* storage = mcp ? mcp->getStorage() : nullptr;
        if (!storage) {
            return ReturnValue("{\"error\":\"storage_unavailable\",\"message\":\"Storage not available.\"}");
        }
        if (!storage->setLocation(location.c_str())) {
            return ReturnValue("{\"error\":\"location_save_failed\",\"message\":\"Failed to save location.\"}");
        }
        String result = "{\"status\":\"ok\",\"location\":\"" + location + "\"}";
        return ReturnValue(result);
    }
);
```

Pattern takeaway:
- Prefer explicit errors and persistence for user preferences.

## Example 7: `self.timer.set` (Validation + State + Time Math + Persistence)

Timers show how to validate mutually-exclusive params, attach an optional label,
persist state, and return computed metadata.

Key ideas:
- Accept exactly one of hours/minutes/seconds.
- Enforce limits (1 second to 8 hours).
- Accept an optional `label` (≤24 chars) so multiple timers are distinguishable.
- Up to 8 timers run concurrently; `start()` returns the assigned `id` (0 = failure / cap reached).
- Call `timer_manager->persistTimers(mcp->getStorage())` after any mutation so timers survive reboot.
- Return `id`, `label`, and `ends_at_epoch_ms` for UI or speech.

```cpp
mcp->addTool(
    "self.timer.set",
    "Start a countdown timer using hours OR minutes OR seconds. Up to 8 timers can run concurrently.\n"
    "Use this tool for:\n"
    "1. Creating a new timer when the user asks (e.g., 'set a 10 minute pasta timer').\n"
    "Notes: provide exactly one of hours, minutes, or seconds. The optional label names the timer.",
    PropertyList({
        Property("hours",   PROPERTY_TYPE_INTEGER, 0, 0, 8),
        Property("minutes", PROPERTY_TYPE_INTEGER, 0, 0, 480),
        Property("seconds", PROPERTY_TYPE_INTEGER, 0, 0, 28800),
        Property("label",   PROPERTY_TYPE_STRING,  "")
    }),
    [mcp](PropertyList& params) -> ReturnValue {
        auto* timer_manager = mcp ? mcp->getTimerManager() : nullptr;
        if (!timer_manager) {
            return ReturnValue("{\"error\":\"timer_unavailable\",\"message\":\"Timer unavailable.\"}");
        }
        // Validate mutually exclusive inputs, then:
        uint8_t id = timer_manager->start(duration_seconds, label.c_str(), format);
        if (id == 0) {
            return ReturnValue("{\"error\":\"timer_start_failed\",\"message\":\"Failed to start timer (max timers reached).\"}");
        }
        timer_manager->persistTimers(mcp->getStorage());  // survive reboot

        JsonDocument* response = new JsonDocument();
        (*response)["status"] = "ok";
        (*response)["id"] = id;
        (*response)["label"] = label;
        (*response)["duration_seconds"] = duration_seconds;
        (*response)["ends_at_epoch_ms"] = ends_at;
        return ReturnValue(response);
    }
);
```

The companion tools follow the same shape:
- `self.timer.list` — returns all active timers as an array (`id`, `label`, `duration_seconds`, `remaining_seconds`, `ends_at_epoch_ms`).
- `self.timer.status` — optional `id` (0 = soonest-expiring running timer).
- `self.timer.cancel` — optional `id` (0 = most-recently started); persists after canceling.
- `self.timer.repeat` — optional `id` (0 = most-recent); restarts that duration/label as a new timer.

Pattern takeaway:
- Use this for tools that require strict param constraints, multi-instance state, and NVS persistence.

## ReturnValue Patterns

Use one of the following:
- `ReturnValue(true)` or `ReturnValue(false)` for simple success/failure.
- `ReturnValue("{...}")` for explicit JSON error payloads.
- `ReturnValue(JsonDocument*)` for structured success responses.

## Parameter Patterns

Define parameters with `PropertyList`:
- `Property("name", PROPERTY_TYPE_STRING, "default")`
- `Property("value", PROPERTY_TYPE_INTEGER, min, max)`

Use `params["name"]` to access inputs and validate them.

## Accessing Device Components

Use the `McpServer` accessors:
- `mcp->getEffectsManager()`
- `mcp->getStorage()`
- `mcp->getDisplay()`
- `mcp->getAudioCodec()`
- `mcp->getTimerManager()`
- `mcp->getUi()`
- `mcp->getGifManager()`
- `mcp->getAudioService()`

Always guard against `nullptr` and return a clear error or `false`.

## Recommended Error Handling

- Validate inputs and return specific JSON errors.
- Validate required subsystems before use.
- Log `ESP_LOGE` for failures and `ESP_LOGI` for success.

## Minimal Checklist for New MCP Tools

- Add `mcp->addTool(...)` in the appropriate registry function.
- Provide clear usage notes in the description string.
- Define parameters with `PropertyList` and validate them.
- Access device subsystems via `mcp` and handle `nullptr`.
- Return `ReturnValue(true/false)` or JSON as appropriate.
