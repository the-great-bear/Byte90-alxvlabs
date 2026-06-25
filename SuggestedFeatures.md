# Suggested Features — Roadmap & Implementation Plan

> Planning doc for four candidate features against the current architecture (see [`ARCHITECTURE.md`](ARCHITECTURE.md)). Each section includes design, files touched, risks, acceptance criteria, and effort estimates in two units (human dev-days and "agent-writes-the-code" time).
>
> **Status:** proposal — not yet started. Owner: TBD.

---

## Reality Check

| # | Feature | Current state | Net work |
|---|---|---|---|
| 1 | Voice-activated timers | **Already shipping.** `self.timer.{set,status,cancel,repeat}` exist in `lib/mcp/McpToolRegistry.cpp`, backed by `lib/system/TimerManager`. Single-timer only, no labels, no persistence across reboot. | **Enhance** |
| ~~2~~ | ~~Bluetooth audio passthrough~~ | ~~**Hardware-blocked.** XIAO ESP32-S3 has BLE 5 only — no Bluetooth Classic → no A2DP. Needs re-scope.~~ | ~~**Decide path first**~~ |
| 3 | Weather updates | `self.get_weather` exists (Google Weather API), but reactive only — no schedule, no on-screen display, no cache. | **Extend** |
| 4 | More jokes | `self.tell_joke` returns a static 3-joke prompt; no categories, no data file. | **Small** |

---

## Feature 1 — Voice-activated timers (multi-timer + persistence)

**Goal:** Move from single global timer → multiple named timers that survive reboot.

### Design

- Refactor `TimerManager` (`lib/system/TimerManager.{h,cpp}`) from one timer to `std::vector<Timer>` where `Timer = { id, label, duration_s, ends_at_ms, repeat }`. Cap at 8 active.
- Persist active timers to NVS via `NvsStorage` on every mutation; rehydrate on boot (drop expired entries).
- New / extended MCP tools in `lib/mcp/McpToolRegistry.cpp` under `registerTimerTools()`:
  - `self.timer.set` — add optional `label` (string ≤24 chars). Returns `{id, label, ends_at_epoch_ms}`.
  - `self.timer.list` — return all active timers (new).
  - `self.timer.cancel` — make `id` or `label` optional (defaults to most recent).
  - `self.timer.status` — accept optional `id`/`label`.
- When a timer fires, raise on `EventBus` → `ApplicationAudio` plays `data/sounds/timer.mp3` + `ApplicationServices` issues a synthetic LLM turn (`"the {label} timer just finished"`) so the assistant speaks the label.
- Update `data/lang/{en-US,zh-CN}/language.json` with new timer strings.

### Files

`lib/system/TimerManager.*`, `lib/storage/NvsStorage.*`, `lib/mcp/McpToolRegistry.cpp`, `src/ApplicationAudio.cpp`, `src/ApplicationServices.cpp`, `data/lang/*`, tests in `test/test_timer_manager/`.

### Risk / Acceptance

- **Risk:** NVS layout change. 8 timers × ~40 B is trivial; budget covers it.
- **Acceptance:** all four MCP tools work; reboot mid-timer resumes correctly; two concurrent labeled timers tested end-to-end; `test_timer_manager` green.

---

## ~~Feature 2 — Bluetooth audio passthrough  ⚠ hardware constraint~~

~~The XIAO ESP32-S3 supports **Bluetooth 5.0 LE only**. Bluetooth Classic / A2DP / HFP is not available on this MCU. Anything sold as "Bluetooth audio" in the consumer sense is A2DP — that path is blocked without a hardware change.~~

### ~~Pick one — decision required before further planning~~

~~**Path A — BLE LE Audio (LC3 codec)**~~
~~- Espressif's LE Audio stack is still maturing; sink role works in recent ESP-IDF; source role is rougher.~~
~~- Phones with LE-Audio support are scarce (Pixel 8+, recent Samsung). Most users get nothing.~~
~~- Couples firmware to specific ESP-IDF versions.~~

~~**Path B — "Wireless audio passthrough" over WiFi** *(recommended)*~~
~~- Honors the user-facing intent without fighting the silicon.~~
~~- Options: lightweight HTTP/WebSocket PCM sink (`lib/network/WebsocketClient` already exists), SnapCast client, or an AirPlay 1 receiver port.~~
~~- New module `lib/audio/NetworkAudioSink` plugs into the existing `AudioCodec` output path — re-uses the Core 1 decode → ring buffer → I²S pipeline; new producer feeds the ring instead of the Opus decoder.~~
~~- MCP tools: `self.audio.passthrough.start` / `.stop` / `.status`.~~
~~- Captive portal toggle + status display.~~
~~- Clean fit with current architecture.~~

~~**Path C — Add an external BT module (PCB revision)**~~
~~- e.g., CSR8645 / BM83 over I²S routed to the speaker amp.~~
~~- Out of scope for firmware-only; flag for a future Series 3 board.~~

### ~~Acceptance (Path B)~~

~~Phone / laptop streams PCM to `byte90.local`; audio comes out the speaker with <300 ms latency; existing AI session pauses passthrough cleanly; passthrough survives a 30-minute stress test.~~

---

## Feature 3 — Weather updates (scheduled + on-screen)

**Goal:** Move from reactive lookup to scheduled refreshes + a clock-style weather display + spoken morning briefing.

### Design

- New module `lib/clock/WeatherSchedule.{h,cpp}` (clock domain owns time-based triggers). Holds list of HH:MM trigger points + optional "on motion wake before noon" trigger.
- Extend `lib/services/ApiClient` with `WeatherClient::fetchAndCache()` and refactor the existing `self.get_weather` handler to delegate, so HTTP logic lives in one place.
- Cache last response to NVS as a single JSON blob + `fetched_at_epoch_s`. Stale-while-revalidate in the cache reader.
- New UI: `lib/ui/WeatherDisplay.{h,cpp}` parallel to `DigitalClock` — icon + temp + condition. Status bar updates while in weather mode.
- New MCP tools:
  - `self.weather.show` / `.hide` — toggle on-screen mode.
  - `self.weather.set_schedule` — `{ times: ["07:30","18:00"], voice_brief: true }` persisted to NVS.
  - `self.weather.get_cached` — fast path for the LLM to answer without burning API quota.
- When a scheduled trigger fires with `voice_brief = true`, `ApplicationServices` issues a synthetic LLM turn ("briefing requested") with cached weather attached, so the agent reads it aloud.

### Files

`lib/clock/WeatherSchedule.*` (new), `lib/services/ApiClient.*`, `lib/ui/WeatherDisplay.*` (new), `lib/storage/NvsStorage.*`, `lib/mcp/McpToolRegistry.cpp`, `src/ApplicationServices.cpp`, `src/ApplicationUI.cpp`.

### Risk / Acceptance

- **Pre-req risk:** `GOOGLE_WEATHER_API_KEY` is currently build-time (see Risk #4 in `ARCHITECTURE.md`). Migrate to NVS so users supply it via the portal — ~½ day of work, blocks user-facing rollout.
- **Acceptance:** two daily times fire and speak; cached value still rendered after a WiFi drop; weather display readable at 1 m; zero API calls when cache <30 min old.

---

## Feature 4 — More jokes (categories + data-driven)

**Goal:** Hard-coded prompt → curated joke library with categories and language packs.

### Design

- New assets: `data/jokes/en-US.json` and `data/jokes/zh-CN.json`. Schema:
  ```json
  {
    "categories": ["retro","programmer","microcontrollers","gaming","dad","clean"],
    "jokes": [
      { "id": "j001", "category": "retro", "setup": "...", "punchline": "..." }
    ]
  }
  ```
- Load via `LanguageManager` (or sibling `JokesProvider`); cache ~200 records in PSRAM at boot.
- Extend `self.tell_joke` parameters:
  - `category` (string, optional — defaults to `"retro"`).
  - `count` (int 1–5, default 3).
- Tool returns selected setups + punchlines as a JSON array; LLM still handles delivery and snark.
- Optional second tool `self.tell_joke.fresh` that fetches from `jokeapi.dev` once per day and stitches the result in — purely additive, no joke-DB dependency.

### Files

`data/jokes/*.json` (new), `lib/language/JokesProvider.*` (new — or extend `LanguageManager`), `lib/mcp/McpToolRegistry.cpp`, tests in `test/test_mcp_tool_manager/`.

### Risk / Acceptance

- **Risk:** PSRAM cost is negligible — 200 jokes ≈ 30 KB of 8 MB.
- **Acceptance:** `category` param works; bilingual returns correct language; tool still returns a valid response when the asset file is missing (falls back to the static set).

---

## Effort Estimates (two units)

Two unit systems, because the relevant cost depends on who's writing:

| Feature | Human dev-days | Agent-writes-the-code time | What still costs real time |
|---|---|---|---|
| F4 jokes | 1–2 | ~30 min | Flashing + verifying bilingual output |
| F1 multi-timer | 2–3 | ~1–2 hr | NVS-rehydration edge cases, unit-test pass |
| F3 weather | 3–4 | ~2–3 hr | API-key wiring, on-device display tuning |
| ~~F2 Path B (WiFi passthrough)~~ | ~~4–5~~ | ~~~3–4 hr~~ | ~~Real-device latency tuning, audio quality~~ |

**Caveat for the agent column:** the typing isn't the bottleneck — the loop is. Per [`AGENTS.md`](AGENTS.md), the maintainer flashes locally; CI does not. So the real cycle is **agent writes → maintainer flashes → maintainer reports → agent patches**. Each round-trip is the actual cost driver, not the code generation.

**Caveat for the human column:** numbers assume a mid-level embedded engineer reading the touched modules first, writing tests, handling watchdog regressions, and putting up a reviewed PR.

---

## Suggested Sequencing

1. **F4 (jokes)** — lowest risk; validates the asset-loading + tool-param-extension pattern. ~1 day.
2. **F1 (multi-timer)** — touches `TimerManager` + NVS persistence; builds the rehydration pattern reused by F3.
3. **F3 (weather)** — depends on F1's NVS rehydration + the `GOOGLE_WEATHER_API_KEY` portal-config migration.
4. ~~**F2 (audio passthrough)** — **blocked** until a Path A vs Path B vs Path C decision lands. Don't start until decided.~~

**Total ship time:** ~6–9 human dev-days for F1, F3, F4. ~~F2 struck — hardware-blocked.~~

---

## Cross-cutting Work to Budget Once

- **Portal-side surfaces** for new toggles (jokes category default, weather schedule) → small `webserver/` work, ~½ day each.
- **Docs** in `docs/MCP_TOOLS_GUIDE.md` and `docs/API_REFERENCE.md` for new tools.
- **Version bump** — `platformio.ini`'s `FIRMWARE_VERSION` and doc references must move together (per Risk #11 in `ARCHITECTURE.md`). Likely target: `3.1.0`.

---

## Open Decisions

1. ~~**F2 path:** A (BLE LE Audio), B (WiFi passthrough — recommended), or C (defer to PCB revision)?~~
2. **F3 pre-req:** migrate `GOOGLE_WEATHER_API_KEY` to NVS portal config now, or punt?
3. **Sequencing:** ship features individually as they land, or batch into a single `3.1.0` release?
