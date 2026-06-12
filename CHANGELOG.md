# Changelog

All notable changes to this firmware are documented here.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/),
and this project uses semantic-ish versioning via the `FIRMWARE_VERSION` build flag.

## [3.1.0] - 2026-06-12

### Added — Google Gemini Live (native audio) provider

A second realtime AI voice provider alongside OpenAI Realtime. The firmware now
ships as one image per provider (mirroring the existing OpenAI / Xiaozhi split);
build the Gemini image with `pio run -e seeed_xiao_esp32s3_gemini`.

- **`lib/services/GeminiWebsocket.{h,cpp}`** — Gemini Live (BidiGenerateContent)
  realtime client. Exposes the exact same public interface as `OpenAIWebsocket`
  and reuses the same SPIRAM PCM ring buffers, FreeRTOS worker/tool tasks and
  base64 plumbing. Internals differ only in transport and wire protocol.
- **`lib/services/GeminiApiClient.{h,cpp}`** — Gemini Live message construction
  (`setup`, `realtimeInput.audio`, `clientContent`, `toolResponse`).
- **`lib/mcp/GeminiAdapter.{h,cpp}`** — maps the on-device MCP tools to Gemini
  `functionDeclarations` (OpenAPI-subset schema) and dispatches tool calls.
- **`lib/services/RealtimeAiProvider.h`** — compile-time provider selector. The
  app refers to the active client through the `RealtimeAiClient` alias; default
  build = OpenAI, `-DAI_PROVIDER_GEMINI` = Gemini.
- **Captive portal:** new `geminiCard` (API key field) in `webserver/` and the
  `/api/gemini-key`, `/api/gemini-key/status`, `/api/gemini-key/clear` endpoints
  in `CaptivePortal`. The portal filesystem (`data/portal/`) was rebuilt.
- **NVS:** dedicated `gemini` namespace with
  save/clear/has/getLast4/get Gemini API-key methods (`NvsStorage`).
- **Config (`DeviceConfig.h`):** `GEMINI_LIVE_*` constants (host, path, model,
  voice, instructions). The BYTE-90 persona is shared with the OpenAI build.
- **`platformio.ini`:** new `env:seeed_xiao_esp32s3_gemini` build environment.

### Audio

- Gemini Live consumes the microphone's native **16 kHz** PCM directly; the
  16 kHz→24 kHz capture resample used by the OpenAI path is bypassed for the
  Gemini build (`ApplicationAudio`). Output remains 24 kHz PCM for both providers.

### Notes / to confirm before flashing

- Two values may drift with Google's API: the native-audio model id
  (`gemini-2.5-flash-native-audio-preview-12-2025`) and the API-version segment
  of the WebSocket path (`v1beta`). Both live in `DeviceConfig.h`.
- Gemini Live authenticates with the API key in the WebSocket URL query string
  (`?key=...`), so no Authorization header is set for this provider.

## [3.0.0] - Series 2 AI Edition

- Initial Series 2 AI Edition firmware with OpenAI Realtime and Xiaozhi providers.
