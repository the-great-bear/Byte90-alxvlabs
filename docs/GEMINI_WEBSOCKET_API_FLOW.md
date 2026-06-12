# Gemini Live WebSocket flow

How BYTE-90 talks to Google's **Gemini Live API** (`BidiGenerateContent`) when
built with `-DAI_PROVIDER_GEMINI`. This mirrors `OPENAI_WEBSOCKET_API_FLOW.md`;
the device architecture (mic → realtime WebSocket → speaker, plus MCP tools) is
identical and only the provider's wire protocol differs.

## Transport

- **Endpoint:** `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=<API_KEY>`
- **Auth:** API key in the URL query string (`?key=...`). No Authorization header.
- **TLS:** validated against the existing `ROOT_CA_BUNDLE` (already contains
  Google Trust Services GTS Root R1), so no new certificate is required.
- **Audio in:** PCM16, mono, **16 kHz** (the device's native mic rate — sent
  without resampling).
- **Audio out:** PCM16, mono, **24 kHz**.
- **Framing:** JSON text frames; audio is base64 inside the JSON.

## Model & voice

- Model: `gemini-2.5-flash-native-audio-preview-12-2025` (native audio).
- Voice: `Puck` (prebuilt). Both configurable in `DeviceConfig.h`.

> Confirm the model id and the `v1beta` path segment against current Google docs
> before flashing — these are the two values most likely to change.

## Message flow

```
Device                                   Gemini Live
  | --- (WS open) ----------------------->  |
  | --- setup ----------------------------> |   model, AUDIO modality, voice,
  |                                         |   system instruction, tools,
  |                                         |   automatic VAD config
  | <-- setupComplete --------------------- |
  | --- clientContent ("Hello") ----------> |   greeting turn (turnComplete=true)
  |                                         |
  | --- realtimeInput.audio (16k PCM) ----> |   streamed mic frames (~20 ms)
  |        ... server VAD detects turn ...   |
  | <-- serverContent.modelTurn ----------- |   inlineData = base64 24k PCM
  | <-- serverContent.modelTurn ----------- |   (more audio chunks)
  | <-- serverContent.generationComplete -- |
  | <-- serverContent.turnComplete -------- |
  |                                         |
  | <-- toolCall.functionCalls ------------ |   (when the model calls an MCP tool)
  | --- toolResponse.functionResponses ---> |   result; generation auto-resumes
  |                                         |
  | <-- serverContent.interrupted --------- |   user barge-in; stop playback
```

### Client → server frames (`GeminiApiClient`)

| Frame | Purpose |
|---|---|
| `setup` | One-time session config (model, modality, voice, system instruction, tools, automatic VAD). Must be the **first** frame. |
| `realtimeInput.audio` | A base64 PCM16 mic chunk (`mimeType: audio/pcm;rate=16000`). |
| `clientContent` | A text turn (used for the startup greeting). |
| `toolResponse.functionResponses` | Result of an MCP tool call; the model auto-continues. |

### Server → client messages (`GeminiWebsocket::handleText`)

| Message | Handling |
|---|---|
| `setupComplete` | Session ready → send greeting. |
| `serverContent.modelTurn.parts[].inlineData` | Decode base64 → push to the 24 kHz playback ring. First chunk of a turn fires *response start* + *speaking(true)*. |
| `serverContent.generationComplete` | Fire *output audio done*. |
| `serverContent.turnComplete` | Fire *response done* + *speaking(false)*. |
| `serverContent.interrupted` | User barge-in → clear playback, *speaking(false)*. |
| `toolCall.functionCalls[]` | Queue for the MCP tool worker. |
| `toolCallCancellation` / `goAway` | Logged. |

## Turn taking

Gemini Live uses **server-side automatic Voice Activity Detection**. The device
does not issue an explicit "create response" — the server starts generating when
it detects end-of-speech, and reports barge-in via `serverContent.interrupted`.
Echo suppression while the model is speaking is handled the same way as the
OpenAI path (capture is paused on the speaking-state callback).
