# Audio Streaming Architecture Documentation

This document provides comprehensive documentation for bidirectional audio streaming in the
Byte-90 firmware, covering the complete flow from microphone to server and server to speaker.

**Last Updated:** 2026-02-11
**Cross-referenced with:** `lib/audio/`, `lib/services/`, `lib/network/`, `src/ApplicationAudio.cpp`

---

**Related protocol docs:**
- `docs/OPENAI_WEBSOCKET_API_FLOW.md`
- `docs/XIAOZHI_WEBSOCKET_API_FLOW.md`
- `docs/XIAOZHI_MQTTUDP_API_FLOW.md`

---

## Table of Contents

1. [Overview](#overview)
2. [Input Audio Streaming (Microphone -> Server)](#input-audio-streaming-microphone-server)
3. [Output Audio Streaming (Server -> Speaker)](#output-audio-streaming-server-speaker)
4. [Queue and Buffer Architecture](#queue-and-buffer-architecture)
5. [WebSocket Binary Protocol Overhead](#websocket-binary-protocol-overhead)
6. [Performance Metrics](#performance-metrics)
7. [Task Priority Architecture](#task-priority-architecture)
8. [Memory Management](#memory-management)
---

## Overview

The Byte-90 device uses a multi-queue, multi-task architecture powered by FreeRTOS to enable
low-latency, real-time bidirectional audio streaming. Audio packets flow through queues and
a ring buffer connecting capture, encode, transport, decode, and output stages.

### Architecture Principles

- **Non-blocking capture:** High-priority capture task never blocks on full queues.
- **Blocking playback:** Decode/output tasks can block to maintain smooth playback.
- **Memory separation:** PSRAM for large buffers, internal RAM for queues and control structures.
- **Task priorities:** Tuned to prevent audio dropouts (capture: 5, output: 5, decode: 2).
- **Centralized tasks:** TaskManager owns creation, cleanup, and health tracking.
- **Zero-copy playback:** Ring buffer slots avoid per-packet malloc/free churn.

### Audio Hardware (AudioCodec)

- **MCU:** ESP32-S3
- **I2S Ports:** I2S0 for speaker output, I2S1 for microphone input
- **Microphone:** ICS-43434 I2S MEMS mic
- **Speaker Amplifier:** MAX98357A I2S amplifier
- **Mode:** Full-duplex I2S (mic + speaker)
- **Mic Channel Quirk:** The ICS-43434 is wired to the LEFT channel, but in full-duplex mode the mic signal can appear on either L or R at boot. The codec reads stereo pairs and sums L+R.

### Key Components

| Component | Location | Purpose |
|-----------|----------|---------|
| **AudioService** | `lib/audio/AudioService.cpp` | Audio processing orchestration |
| **AudioCodec** | `lib/audio/AudioCodec.cpp` | ESP32-S3 I2S full-duplex audio driver |
| **ApplicationAudio** | `src/ApplicationAudio.cpp` | Glue between audio and protocol |
| **AudioPacketSink** | `lib/audio/AudioService.h` | Protocol-agnostic audio egress interface |
| **TenclassWebsocket** | `lib/services/TenclassWebsocket.cpp` | WebSocket protocol transport |
| **MQTTClient** | `lib/network/MQTTClient.cpp` | MQTT control + UDP audio transport |
| **OpusEncoder** | `lib/audio/OpusEncoder.cpp` | PCM -> Opus compression |
| **OpusDecoder** | `lib/audio/OpusDecoder.cpp` | Opus -> PCM decompression |
| **OpusResampler** | `lib/audio/OpusResampler.cpp` | Sample rate conversion (SILK) |

---

## Input Audio Streaming (Microphone -> Server)

### Flow Diagram

```
Microphone (ICS-43434 I2S)
    ->
I2S Input (Hardware Sample Rate)
    ->
Capture Task (Priority 5, Core 0)
    -> Read PCM
    -> Resample if needed (codec rate -> 16 kHz)
    -> Opus encode (60 ms frames)
    -> Send queue (OpusPacket)
    ->
ApplicationAudio::updateTransmission()
    -> Build AudioStreamPacket
    -> AudioPacketSink::sendAudio(packet)
    ->
Transport:
    - TenclassWebsocketAudioSink -> WebSocket binary v1/v2
    - TenclassMqttAudioSink -> MQTT control + UDP audio

OpenAI Realtime (PCM capture)
    -> PcmSampleCallback (raw PCM)
    -> processOpenAICapture(16 kHz -> 24 kHz)
    -> OpenAIWebsocket::enqueuePcm()
```

### Detailed Code Flow

#### 1. Capture Task (Priority 5, Core 0)

**Location:** `lib/audio/AudioService.cpp`

Key steps:
- Read PCM from codec at the actual input rate.
- Downsample to `AUDIO_SAMPLE_RATE_STT` (16000 Hz) if needed.
- Encode to Opus and push `OpusPacket` into `_send_queue`.
- Use `_timestamp_queue` entries (playback timestamps) when present.

#### OpenAI Realtime Capture (PCM)

**Location:** `src/ApplicationAudio.cpp`, `lib/audio/AudioService.cpp`

When `USE_OPENAI_REALTIME` is enabled, capture uses PCM instead of Opus:
- `AudioService::setPcmSampleCallback()` provides raw PCM samples.
- `AudioService::processOpenAICapture()` resamples 16 kHz → 24 kHz.
- `OpenAIWebsocket::enqueuePcm()` buffers and transmits PCM frames.

#### 2. Application Loop (Main Core)

**Location:** `src/ApplicationAudio.cpp`

`ApplicationAudio::updateTransmission()`:
- Pops every available `OpusPacket` from `_send_queue`.
- Builds `AudioStreamPacket` with:
  - `sample_rate = AUDIO_SAMPLE_RATE_STT`
  - `frame_duration = AUDIO_OPUS_FRAME_MS`
  - `timestamp` from the send queue
- Calls `AudioPacketSink::sendAudio(packet)` (adapter selects WebSocket or MQTT path).

#### 3. Transport Send

**WebSocket (default):** `lib/services/TenclassWebsocket.cpp`
- v1: raw Opus payload
- v2: BinaryProtocol2 header + Opus payload

**MQTT (USE_MQTT_PROTOCOL):** `lib/network/MQTTClient.cpp`
- Control over MQTT topics
- Audio frames sent via UDP (see `example/mqtt-udp.md`)

### Key Characteristics - Input Stream

| Characteristic | Value | Notes |
|----------------|-------|-------|
| **Sample Rate** | 16000 Hz | `AUDIO_SAMPLE_RATE_STT` |
| **Channels** | 1 (mono) | Voice optimized |
| **Frame Duration** | 60 ms | `AUDIO_OPUS_FRAME_MS` |
| **Bitrate** | 8 kbps | Opus encoder setting |
| **Packet Rate** | ~16.67 packets/sec | 1000 ms / 60 ms |
| **DTX** | Enabled | Silence packets dropped |
| **Queue Depth** | 20 packets | ~1200 ms buffering |
| **Blocking** | Non-blocking | Drops on full queue |

---

## Output Audio Streaming (Server -> Speaker)

### Flow Diagram

```
Server
    ->
Protocol receive (WebSocket/MQTT)
    ->
AudioStreamPacket callback
    ->
ApplicationAudio::handleIncomingAudio()
    -> AudioService::queuePlaybackPacket()
    ->
Opus decode queue (OpusPacket)
    ->
Opus Decode Task (Priority 2, Core 1)
    -> Decode into ring buffer slot
    -> Resample if needed
    -> Signal ring buffer full
    ->
Audio Output Task (Priority 5, Core 1)
    -> Write PCM to codec
    -> Record timestamp for AEC
    -> Recycle ring buffer slot
```

### Detailed Code Flow

#### 1. Protocol Receive -> Application Callback

**WebSocket:** `lib/services/TenclassWebsocket.cpp`
- Handles binary frames and extracts `timestamp` for v2.
- Builds `AudioStreamPacket` and calls the registered callback.

**MQTT:** `lib/network/MQTTClient.cpp`
- UDP audio packets are decrypted and parsed.
- Builds `AudioStreamPacket` with timestamp.

#### 2. Application Handler

**Location:** `src/ApplicationAudio.cpp`

`ApplicationAudio::handleIncomingAudio()`:
- Checks playback state.
- Calls `AudioService::queuePlaybackPacket()` with payload and timestamp.
- Deletes `AudioStreamPacket` after queueing.

#### 3. Opus Decode Task (Priority 2, Core 1)

**Location:** `lib/audio/AudioService.cpp`

Key steps:
- Pops Opus packets from `_opus_decode_queue`.
- Waits for an empty ring buffer slot.
- Decodes directly into the slot (no malloc).
- Resamples into the slot if needed.
- Signals `full_sem` for the output task.

#### 4. Audio Output Task (Priority 5, Core 1)

**Location:** `lib/audio/AudioService.cpp`

Key steps:
- Waits for a full ring buffer slot.
- Writes PCM to codec (`AudioCodec::write()`).
- Pushes playback timestamps into `_timestamp_queue` for AEC.
- Marks slot free and signals `empty_sem`.

#### OpenAI Realtime PCM Playback (OpenAI pipeline)

**Location:** `lib/audio/AudioService.cpp`, `lib/services/OpenAIWebsocket.cpp`

When `USE_OPENAI_REALTIME` is enabled, OpenAI output audio is streamed as PCM:
- `OpenAIWebsocket` receives audio deltas and pushes PCM into a ring buffer (PSRAM).
- `AudioService::startPcmPlayback()` creates `pcm_playback` (priority 5, core 1).
- The PCM playback task reads 10 ms chunks from the ring buffer and writes to the codec.

### Key Characteristics - Output Stream

| Characteristic | Value | Notes |
|----------------|-------|-------|
| **Sample Rate** | Codec output rate | Typically 24000 Hz |
| **Channels** | 1 (mono) | Voice/music |
| **Bitrate** | Variable | Server-controlled |
| **Queue Depth** | 20 Opus packets | ~1200 ms buffering |
| **Ring Buffer** | 4 slots | ~240 ms buffering |
| **Blocking** | Blocking | Decode/output wait on queues |

---

## Queue and Buffer Architecture

### Queues

**_send_queue (Capture -> Application)**
- Size: 20 `OpusPacket`
- Producer: capture task (priority 5)
- Consumer: application loop
- Non-blocking send; drops on full queue.

**_opus_decode_queue (Application -> Decode Task)**
- Size: 20 `OpusPacket`
- Producer: incoming audio handler
- Consumer: Opus decode task (priority 2).

**_timestamp_queue (Playback -> Capture)**
- Size: 3 `uint32_t`
- Producer: audio output task
- Consumer: capture task (adds timestamp to outgoing packets).

**_pcm_output_queue**
- Size: 2 `PcmPacket`
- Present for legacy flow; ring buffer is the active playback path.

### Ring Buffer

**PcmRingBuffer (Playback)**
- Slots: 4
- Each slot contains:
  - `pcm_data` buffer (PSRAM)
  - `resample_data` buffer (PSRAM)
  - `samples`, `timestamp`, `needs_resample`
- Synchronization:
  - `empty_sem` for slot availability
  - `full_sem` for ready-to-play slots

---

## WebSocket Binary Protocol Overhead

Applies only to WebSocket transport. MQTT audio uses UDP framing
documented in `example/mqtt-udp.md`.

### Protocol Version 1 (Minimal Overhead)

```
[Opus Encoded Audio Data]
```

- **Header Size**: 0 bytes
- **Use Case**: Minimal bandwidth, no timestamp support

### Protocol Version 2 (With Timestamp for AEC)

```
[2B version][2B type][4B reserved][4B timestamp][4B payload_size][Opus data]
```

- **Header Size**: 20 bytes
- **Timestamp**: `millis()` at capture time
- **Endianness**:
  - `version`: `htons(2)`
  - `timestamp`, `payload_size`: `htonl()`

---

## Performance Metrics

### Input Streaming (Microphone -> Server)

| Metric | Value | Notes |
|--------|-------|-------|
| **Sample Rate** | 16000 Hz | Encoder target rate |
| **Frame Duration** | 60 ms | 960 samples @ 16 kHz |
| **Opus Bitrate** | 8 kbps | Configured bitrate |
| **Packet Rate** | ~16.67 packets/sec | 1000 ms / 60 ms |
| **Queue Depth** | 20 packets | ~1200 ms |
| **Protocol v2 Overhead** | 20 bytes | ~2.7 kbps at 60 ms frames |

### Output Streaming (Server -> Speaker)

| Metric | Value | Notes |
|--------|-------|-------|
| **Sample Rate** | Codec output rate | Typically 24000 Hz |
| **Queue Depth** | 20 packets | Opus decode queue |
| **Ring Buffer** | 4 slots | Playback buffering |
| **Bitrate** | Variable | Server-controlled |

---

## Task Priority Architecture

### Task Priority Table

| Task | Priority | Core | Purpose |
|------|----------|------|---------|
| **Capture Task** | 5 | 0 | Read I2S, encode Opus |
| **Opus Decode Task** | 2 | 1 | Decode Opus to PCM |
| **Audio Output Task** | 5 | 1 | Write PCM to I2S |
| **Main Loop** | 1 | 1 | Application logic, protocol polling |

### Core Assignment

```
Core 0 (Real-time Capture):
  - Capture Task (Priority 5)

Core 1 (Playback + Application):
  - Opus Decode Task (Priority 2)
  - Audio Output Task (Priority 5)
  - Main Loop (Priority 1)
```

---

## Memory Management

### Internal RAM

Used for queues, semaphores, and task stacks:
- `_send_queue` (20 `OpusPacket`)
- `_opus_decode_queue` (20 `OpusPacket`)
- `_timestamp_queue` (3 `uint32_t`)
- Task stacks (capture: 24576, decode: 24576, output: 4096)

### PSRAM

Used for large audio buffers:
- `_pcm_buffer` (capture)
- `_opus_buffer` (encode)
- Optional `_resample_buffer` (capture)
- Ring buffer slots:
  - `pcm_data` and `resample_data` per slot

### Memory Ownership Rules

- `AudioStreamPacket` is owned by the protocol layer on send, and by the application
  on receive (deleted after `queuePlaybackPacket`).
- Playback PCM buffers are reused via the ring buffer (no per-packet malloc/free).
- Opus packets are copied into queues (no pointer ownership transfer).

---

*Document generated from codebase analysis on 2026-01-10*
