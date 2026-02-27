# Audio Processing Documentation

This document provides comprehensive documentation for audio encoding, decoding, and resampling in the Byte-90 firmware.

**Last Updated:** 2026-02-11
**Cross-referenced with:** `lib/audio/` implementation

---

**Related protocol docs:**
- `docs/OPENAI_WEBSOCKET_API_FLOW.md`
- `docs/XIAOZHI_WEBSOCKET_API_FLOW.md`
- `docs/XIAOZHI_MQTTUDP_API_FLOW.md`

---

## Table of Contents

1. [Overview](#overview)
2. [AXP2101 Configuration](#axp2101-configuration)
3. [Audio Capture Path (STT)](#audio-capture-path-stt)
4. [Audio Playback Path (TTS)](#audio-playback-path-tts)
5. [Opus Encoder](#opus-encoder)
6. [Opus Decoder](#opus-decoder)
7. [Opus Resampler](#opus-resampler)
8. [Binary Protocol Requirements](#binary-protocol-requirements)
9. [Sample Rate Configuration](#sample-rate-configuration)
10. [OpenAI Realtime API Configuration](#openai-realtime-api-configuration)
11. [Memory Management](#memory-management)
12. [Performance Characteristics](#performance-characteristics)
13. [Code References](#code-references)
---

## Overview

The Byte-90 device handles audio in two directions:

- **Capture (STT)**: Microphone → Opus Encoder → `AudioPacketSink` → Server
- **Playback (TTS)**: Server → Protocol (WebSocket/MQTT) → Opus Decoder → Speaker

There is no on-device DSP stage (AEC/NS/AGC) in the capture pipeline; audio is
captured, optionally resampled, and encoded to Opus.

**Platform constraint:** The current PlatformIO Arduino core for ESP32-S3 does not include ESP-SR or ESP-ADF. As a result, there is no support for VEC or wake word detection in this Arduino-based platform.

`AudioPacketSink` (declared in `lib/audio/AudioService.h`) is the boundary used
by `ApplicationAudio` to push encoded packets to the active transport adapter.

### Key Components

#### 1. OpusEncoder (`lib/audio/OpusEncoder.cpp`)

**Purpose:** Encode PCM audio to Opus format for transmission

**Configuration:**
- **Sample Rate:** `AUDIO_SAMPLE_RATE_STT` (16000 Hz default)
- **Channels:** 1 (mono)
- **Frame Duration:** `AUDIO_OPUS_FRAME_MS` (60ms default)
- **Bitrate:** 8000 bps (8 kbps)
- **Application:** OPUS_APPLICATION_VOIP
- **Complexity:** 0 (lowest CPU usage)
- **Signal Type:** OPUS_SIGNAL_VOICE
- **DTX (Discontinuous Transmission):** Enabled (silence detection)

#### 2. OpusDecoder (`lib/audio/OpusDecoder.cpp`)

**Purpose:** Decode Opus audio from server to PCM for playback

**Configuration:**
- **Sample Rate:** Codec output rate (typically `AUDIO_SAMPLE_RATE_TTS` = 24000 Hz)
- **Channels:** 1 (mono)
- **Frame Duration:** Determined by incoming packets
- **Forward Error Correction (FEC):** Supported for packet loss

#### 3. OpusResampler (`lib/audio/OpusResampler.cpp`)

**Purpose:** Convert audio between different sample rates

**Implementation:**
- Uses SILK resampler from esp32_opus library
- **Bidirectional:** Can upsample or downsample

### Key Technologies

- **Audio Codec**: ESP32-S3 I2S audio via `AudioCodec` (full-duplex)
- **Microphone**: ICS-43434 I2S MEMS mic (I2S1 input)
- **Speaker**: MAX98357A I2S amplifier (I2S0 output)
- **I2S Topology**: Dual I2S ports (I2S0 output, I2S1 input)
- **Compression**: Opus (highly efficient, low-latency)
- **Resampling**: SILK resampler (from esp32_opus library)
- **Transport**: WebSocket binary (v1/v2) or MQTT control + UDP audio
- **Memory**: PSRAM for large buffers, Internal RAM for queues

### Audio Hardware (AudioCodec)

- **MCU:** ESP32-S3
- **I2S Ports:** I2S0 for speaker output, I2S1 for microphone input
- **Microphone:** ICS-43434 I2S MEMS mic
- **Speaker Amplifier:** MAX98357A I2S amplifier
- **Mode:** Full-duplex I2S (mic + speaker)
- **Mic Channel Quirk:** The ICS-43434 is physically wired to the LEFT channel, but in full-duplex mode the mic data can appear on either L or R at boot. The firmware reads stereo pairs and sums L+R to avoid missing audio.

## AXP2101 Configuration

The AXP2101 power management configuration is required for stable mic power and button detection. On Byte-90 only **DC1** is wired; all other rails are explicitly disabled during initialization.

Key behaviors from `lib/power/Axp2101.cpp`:

- **DC1 only:** DC2/DC3/DC4/DC5 and all LDOs are disabled.
- **Power key IRQs:** Only power key IRQs are enabled (`SHORT`, `LONG`, `POSITIVE`) for button detection.
- **Power key timing:** On-time and long-press thresholds are configured via AXP2101.
- **IRQ clearing:** IRQ status is cleared after reads to avoid stale events.

If AXP2101 is not initialized, both mic and button handling can be unreliable.

---

## Audio Capture Path (STT)

### Flow Diagram

```
Microphone (ICS-43434 I2S)
    ↓
I2S Input (Hardware Sample Rate)
    ↓
AudioCodec::read() - Read PCM samples
    ↓
[RESAMPLING if needed] (Hardware Rate → 16kHz)
    ↓
OpusEncoder::encode() - PCM to Opus
    ↓
Send Queue (FreeRTOS Queue)
    ↓
AudioPacketSink::sendAudio() (WebSocket/MQTT adapter)
    ↓
Server (STT Processing)

OpenAI Realtime Capture (PCM)
    ↓
PcmSampleCallback (raw PCM at capture rate)
    ↓
AudioService::processOpenAICapture() - Resample 16kHz → 24kHz
    ↓
OpenAIWebsocket::enqueuePcm() - Send PCM frames
```

### Implementation Details

**Location:** `lib/audio/AudioService.cpp`

#### Step 1: Read from Codec

```cpp
int samples_read = _codec->read(_pcm_buffer, samples_to_read);
```

- **Codec runs at**: Actual hardware rate (may differ from target 16kHz)
- **Frame Duration**: 60ms worth of audio per read
- **Buffer**: `_pcm_buffer` in PSRAM

#### Step 2: Resampling (Conditional)

```cpp
if (needs_resampling && _resample_buffer != nullptr) {
    int output_samples = _input_resampler.GetOutputSamples(samples_read);
    _input_resampler.Process(_pcm_buffer, samples_read, _resample_buffer);
    encode_buffer = _resample_buffer;
    encode_samples = output_samples;
}
```

- **When**: If `codec_input_rate ≠ 16000 Hz`
- **Input Resampler**: `codec_input_rate → 16000 Hz`
- **Example**: 48000 Hz → 16000 Hz (3:1 downsampling)
- **Buffer**: `_resample_buffer` allocated based on ratio

#### Step 3: Opus Encoding

```cpp
    int encoded_size = _encoder->encode(encode_buffer, encode_samples,
                                        _opus_buffer, AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE);
```

- **Input**: 960 samples (16000 Hz × 60ms) = 1920 bytes
- **Output**: ~60-100 bytes (8 kbps bitrate)
- **Compression Ratio**: ~20:1
- **DTX**: If silent, packet may be only 2 bytes

#### Step 4: Queueing

```cpp
if (xQueueSend(_send_queue, &opusPacket, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Send queue full, dropping packet");
}
```

- **Queue Size**: `SEND_QUEUE_SIZE = 20` packets
- **Non-blocking**: Drops packets if queue full

### Configuration

**Location:** `lib/audio/AudioService.cpp`

```cpp
// Configure input resampler if codec's actual input rate differs from target
if (codec_input_rate != _sample_rate) {
    ESP_LOGI(TAG, "Configuring input resampler: %d Hz -> %d Hz",
             codec_input_rate, _sample_rate);
    _input_resampler.Configure(codec_input_rate, _sample_rate);

    // Allocate resampling buffer
    _resample_buffer_size = _input_resampler.GetOutputSamples(max_input_samples);
    _resample_buffer = (int16_t*)malloc(_resample_buffer_size * _channels * sizeof(int16_t));
}
```

---

## Audio Playback Path (TTS)

### Flow Diagram

```
Server (TTS Audio)
    ↓
Protocol receive (WebSocket/MQTT)
    ↓
Opus Decode Queue (FreeRTOS Queue)
    ↓
OpusDecoder::decode() - Opus to PCM
    ↓
[RESAMPLING if needed] (decoder rate → Codec Output Rate)
    ↓
Ring Buffer Slot (semaphore handoff)
    ↓
AudioCodec::write() - Write PCM samples
    ↓
I2S Output (Hardware Sample Rate)
    ↓
Speaker (MAX98357A I2S)

OpenAI Realtime Playback (PCM)
    ↓
OpenAIWebsocket ring buffer
    ↓
AudioService::startPcmPlayback() / pcmPlaybackTask
    ↓
AudioCodec::write() - Write PCM samples
    ↓
I2S Output (Hardware Sample Rate)
    ↓
Speaker (MAX98357A I2S)
```

### Implementation Details

**Location:** `lib/audio/AudioService.cpp`

#### Step 1: Receive Opus Packet

```cpp
if (xQueueReceive(_opus_decode_queue, &opusPacket, pdMS_TO_TICKS(100)) == pdTRUE) {
```

- **Queue Size**: `OPUS_DECODE_QUEUE_SIZE = 20` packets
- **Timeout**: 100ms

#### Step 2: Opus Decoding

```cpp
    int decoded_samples = _decoder->decode(opusPacket.data, opusPacket.len,
                                           slot->pcm_data, max_decode_samples);
```

- **Decoder Rate**: Codec output rate (typically 24000 Hz)
- **Output**: PCM samples at decoder rate
- **Buffer**: ring buffer slot (`slot->pcm_data`) in PSRAM

#### Step 3: Resampling (Conditional)

```cpp
if (decoder_sample_rate != codec_output_rate) {
    int resampled_size = _output_resampler.GetOutputSamples(decoded_samples);
    _output_resampler.Process(slot->pcm_data, decoded_samples, slot->resample_data);
    slot->samples = resampled_size;
}
```

- **When**: If decoder rate ≠ codec output rate
- **Output Resampler**: `decoder_rate → codec_output_rate`
- **Currently**: Both are typically 24kHz, so resampling is skipped
- **Future**: Automatic resampling if codec output rate changes

#### Step 4: Ring Buffer Handoff

```cpp
// Slot already filled; signal output task
xSemaphoreGive(_ring_buffer.full_sem);
```

#### Step 5: Write to Speaker

**Location:** `lib/audio/AudioService.cpp` (`audioOutputTaskImpl`)

```cpp
_codec->write(output_data, slot->samples);
```

- **I2S Output**: Hardware sample rate
- **Buffer Management**: Slot recycled after write (no free)
- **AEC Timestamp**: Playback timestamps are queued for capture-side AEC

### Configuration

**Location:** `lib/audio/AudioService.cpp`

```cpp
// Configure output resampler if decoder rate differs from codec output rate
if (decoder_sample_rate != codec_output_rate) {
    ESP_LOGI(TAG, "Configuring output resampler: %d Hz -> %d Hz",
             decoder_sample_rate, codec_output_rate);
    _output_resampler.Configure(decoder_sample_rate, codec_output_rate);
}
```

---

## Opus Encoder

**Location:** `lib/audio/OpusEncoder.h`, `lib/audio/OpusEncoder.cpp`

### Class Definition

```cpp
class OpusEncoder {
public:
    OpusEncoder(int sample_rate, int channels, int frame_ms);
    ~OpusEncoder();

    bool begin();
    void end();

    int encode(const int16_t* pcm, int samples, uint8_t* output, int max_output_size);

    bool setBitrate(int bitrate);
    bool setComplexity(int complexity);
    bool setSignal(int signal_type);
    bool setDtx(bool enable);

    void reset();

    int getSampleRate() const { return _sample_rate; }
    int getChannels() const { return _channels; }
    int getFrameSize() const { return _frame_size; }
    bool isInitialized() const { return _encoder != nullptr; }

private:
    ::OpusEncoder* _encoder;
    int _sample_rate;
    int _channels;
    int _frame_size;
    int _frame_ms;
};
```

### Configuration

**Sample Rate**: 16000 Hz
**Channels**: 1 (mono)
**Frame Duration**: 60ms
**Frame Size**: 960 samples (16000 Hz × 60ms)

### Initialization

**Location:** `lib/audio/OpusEncoder.cpp`

```cpp
bool OpusEncoder::begin() {
    int error;
    _encoder = ::opus_encoder_create(_sample_rate, _channels, OPUS_APPLICATION_VOIP, &error);

    // Set default configuration
    setBitrate(8000);              // 8 kbps - good balance of quality and bandwidth
    setComplexity(0);              // Lowest CPU usage
    setSignal(OPUS_SIGNAL_VOICE);  // Optimized for voice
    setDtx(true);                  // Enable silence detection

    return true;
}
```

### Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| **Application** | `OPUS_APPLICATION_VOIP` | Optimized for real-time voice |
| **Bitrate** | `8000` bps | 8 kbps |
| **Complexity** | `0` | Lowest CPU usage (0-10 scale) |
| **Signal Type** | `OPUS_SIGNAL_VOICE` | Voice optimization |
| **DTX** | `true` | Discontinuous Transmission (silence detection) |

### Encoding Process

**Location:** `lib/audio/OpusEncoder.cpp`

```cpp
int OpusEncoder::encode(const int16_t* pcm, int samples,
                       uint8_t* output, int max_output_size) {
    opus_int32 encoded_size = ::opus_encode(_encoder, pcm, samples,
                                            output, max_output_size);

    // DTX: if encoded size is 2 bytes or less, packet doesn't need to be transmitted
    if (encoded_size <= 2) {
        ESP_LOGD(TAG, "DTX: encoded size %ld bytes (silence)", encoded_size);
    }

    return encoded_size;
}
```

### DTX (Discontinuous Transmission)

- **Enabled**: `true`
- **Behavior**: Silence detected automatically
- **Packet Size**: ≤ 2 bytes for silence
- **Benefit**: Reduces bandwidth during silence periods

---

## Opus Decoder

**Location:** `lib/audio/OpusDecoder.h`, `lib/audio/OpusDecoder.cpp`

### Class Definition

```cpp
class OpusDecoder {
public:
    OpusDecoder(int sample_rate, int channels);
    ~OpusDecoder();

    bool begin();
    void end();

    int decode(const uint8_t* opus_data, int opus_len, int16_t* pcm, int max_samples);
    int decodeFec(int16_t* pcm, int max_samples);

    void reset();

    int getSampleRate() const { return _sample_rate; }
    int getChannels() const { return _channels; }
    bool isInitialized() const { return _decoder != nullptr; }

private:
    ::OpusDecoder* _decoder;
    int _sample_rate;
    int _channels;
};
```

### Configuration

**Sample Rate**: Codec output rate (typically 24000 Hz)
**Channels**: 1 (mono)
**Frame Duration**: Variable (determined by incoming packets)

### Initialization

**Location:** `lib/audio/OpusDecoder.cpp`

```cpp
bool OpusDecoder::begin() {
    int error;
    _decoder = ::opus_decoder_create(_sample_rate, _channels, &error);

    ESP_LOGI(TAG, "Opus decoder initialized: %d Hz, %d channels",
             _sample_rate, _channels);

    return true;
}
```

### Decoding Process

**Location:** `lib/audio/OpusDecoder.cpp`

```cpp
int OpusDecoder::decode(const uint8_t* opus_data, int opus_len,
                       int16_t* pcm, int max_samples) {
    int decoded_samples = ::opus_decode(_decoder, opus_data, opus_len,
                                        pcm, max_samples, 0);

    return decoded_samples;
}
```

### Forward Error Correction (FEC)

**Location:** `lib/audio/OpusDecoder.cpp`

```cpp
int OpusDecoder::decodeFec(int16_t* pcm, int max_samples) {
    // Decode FEC (forward error correction) - use NULL for packet loss concealment
    int decoded_samples = ::opus_decode(_decoder, nullptr, 0, pcm, max_samples, 0);

    return decoded_samples;
}
```

- **Purpose**: Packet loss concealment
- **Usage**: When packet is lost, generate estimated audio
- **Benefit**: Maintains audio continuity during network issues

---

## Opus Resampler

**Location:** `lib/audio/OpusResampler.h`, `lib/audio/OpusResampler.cpp`

### Class Definition

```cpp
class OpusResampler {
public:
    OpusResampler();
    ~OpusResampler();

    void Configure(int input_sample_rate, int output_sample_rate);
    void Process(const int16_t *input, int input_samples, int16_t *output);
    int GetOutputSamples(int input_samples) const;

    int input_sample_rate() const { return input_sample_rate_; }
    int output_sample_rate() const { return output_sample_rate_; }

private:
    silk_resampler_state_struct resampler_state_;
    int input_sample_rate_;
    int output_sample_rate_;
};
```

### Implementation

Uses **SILK resampler** from esp32_opus library (`SigProc_FIX.h`)

### Configuration

**Location:** `lib/audio/OpusResampler.cpp`

```cpp
void OpusResampler::Configure(int input_sample_rate, int output_sample_rate) {
    int encode = input_sample_rate > output_sample_rate ? 1 : 0;
    auto ret = silk_resampler_init(&resampler_state_, input_sample_rate,
                                   output_sample_rate, encode);

    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    ESP_LOGI(TAG, "Resampler configured with input sample rate %d and output sample rate %d",
             input_sample_rate_, output_sample_rate_);
}
```

### Processing

**Location:** `lib/audio/OpusResampler.cpp`

```cpp
void OpusResampler::Process(const int16_t *input, int input_samples, int16_t *output) {
    auto ret = silk_resampler(&resampler_state_, output, input, input_samples);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to process resampler");
    }
}
```

### Output Sample Calculation

**Location:** `lib/audio/OpusResampler.cpp`

```cpp
int OpusResampler::GetOutputSamples(int input_samples) const {
    return input_samples * output_sample_rate_ / input_sample_rate_;
}
```

**Example**:
- Input: 2880 samples @ 48kHz
- Output: 960 samples @ 16kHz
- Ratio: 2880 × 16000 / 48000 = 960

### Use Cases

#### Input Resampler (Capture Path)
- **When**: Codec actual input rate ≠ 16000 Hz
- **Example**: 48000 Hz → 16000 Hz (downsampling)
- **Direction**: Downsampling (encode = 1)

#### Output Resampler (Playback Path)
- **When**: Decoder rate ≠ codec output rate
- **Example**: 24000 Hz → 24000 Hz (no resampling)
- **Direction**: Can upsample or downsample

### Quality

- **Algorithm**: SILK (from Skype SILK codec)
- **Quality**: Professional audio quality
- **CPU**: Moderate (depends on ratio)
- **Latency**: Low (<1ms per frame)

---

## Binary Protocol Requirements

Applies to **WebSocket** transport only. MQTT audio uses UDP framing; see
`example/mqtt-udp.md` and `docs/JSON_STRUCTURES.md`.

### Protocol Version Selection

**Specified in provisioning response:**

```json
{
  "websocket": {
    "version": 1  // or 2
  }
}
```

### Binary Protocol Version 1 (Default)

#### Format

```
[Opus Audio Bytes]
```

#### Characteristics

- **Header Size**: 0 bytes
- **Total Size**: ~60-100 bytes (Opus only)
- **Overhead**: Minimal
- **Use Case**: Smallest bandwidth, no metadata needed
- **When**: Default if no version specified

### Binary Protocol Version 2 (With Timestamp)

**Location:** `lib/services/TenclassWebsocket.h`

#### Format

```cpp
struct __attribute__((packed)) BinaryProtocol2 {
    uint16_t version;        // = 2
    uint16_t type;           // Message type: 0 = OPUS, 1 = JSON
    uint32_t reserved;       // Reserved for future use
    uint32_t timestamp;      // Timestamp in milliseconds (for server-side AEC)
    uint32_t payload_size;   // Size of payload in bytes
    uint8_t payload[];       // Payload data (Opus or JSON)
};
```

#### Field Details

| Field | Size | Type | Value | Description |
|-------|------|------|-------|-------------|
| `version` | 2 bytes | uint16_t | `2` | Protocol version |
| `type` | 2 bytes | uint16_t | `0` = OPUS<br>`1` = JSON | Message type |
| `reserved` | 4 bytes | uint32_t | `0` | Reserved |
| `timestamp` | 4 bytes | uint32_t | milliseconds | For AEC sync |
| `payload_size` | 4 bytes | uint32_t | bytes | Payload size |
| `payload` | variable | uint8_t[] | data | Opus or JSON |

#### Characteristics

- **Header Size**: 20 bytes
- **Total Size**: ~80-120 bytes (20 + Opus)
- **Timestamp**: Milliseconds since device boot (`millis()`)
- **Use Case**: Server-side Acoustic Echo Cancellation (AEC)
- **When**: `websocket.version: 2` in provisioning

#### Timestamp Purpose

```cpp
uint32_t timestamp = millis();  // Milliseconds since boot
```

- **Usage**: Server synchronizes audio for AEC
- **Type**: Monotonically increasing
- **Resolution**: 1 millisecond

### Protocol Comparison

| Version | Header | Total Size | Timestamp | Type Info | Overhead |
|---------|--------|------------|-----------|-----------|----------|
| **v1** | 0 bytes | ~60-100 bytes | ❌ | ❌ | Minimal |
| **v2** | 20 bytes | ~80-120 bytes | ✅ | ✅ | High |

### Implementation Requirements

#### 1. Endianness

- **Version**: `htons(2)` on send
- **Timestamp/payload_size**: `htonl()` on send, `ntohl()` on receive
- **Type/reserved**: Sent as zero, not parsed on receive

#### 2. Packing

```cpp
struct __attribute__((packed)) BinaryProtocol2 {
    // No padding between fields
};
```

- **Attribute**: `__attribute__((packed))`
- **Purpose**: No padding/alignment
- **Requirement**: Exact byte layout

#### 3. Type Field Values

- **0**: OPUS audio data
- **1**: JSON data (for hybrid protocols)

---

## Sample Rate Configuration

### Summary Table

| Path | Source | Encoder/Decoder | Resampler | Target | Notes |
|------|--------|----------------|-----------|--------|-------|
| **Capture (STT)** | Codec Input<br>(actual) | 16000 Hz | Input Resampler<br>(if needed) | 16000 Hz | Downsample if<br>codec ≠ 16kHz |
| **Playback (TTS)** | Codec Output<br>(decoder rate) | Codec Output | Output Resampler<br>(if needed) | Codec Output | Typically 24kHz<br>(no resample) |

### Device Transmit (Capture/STT)

**Configuration:**
- **Codec Input Rate**: Variable (hardware dependent)
- **Target Rate**: 16000 Hz
- **Resampling**: Automatic if needed
- **Frame Duration**: 60ms
- **Frame Size**: 960 samples

**Example Scenarios:**

#### Scenario 1: No Resampling
```
Codec @ 16000 Hz → [No Resampling] → Encoder @ 16000 Hz
```

#### Scenario 2: Downsampling
```
Codec @ 48000 Hz → [Resampler: 48k→16k] → Encoder @ 16000 Hz
                    (2880 samples)  (960 samples)
```

### Server Transmit (Playback/TTS)

**Configuration:**
- **Decoder Rate**: Codec output rate (typically 24000 Hz)
- **Codec Output Rate**: Codec output rate
- **Resampling**: Not currently needed (same rates)
- **Future**: Automatic resampling if codec output rate changes

**Example Scenarios:**

#### Scenario 1: No Resampling (Current)
```
Decoder @ 24000 Hz → [No Resampling] → Codec @ 24000 Hz
```

#### Scenario 2: Future Resampling
```
Decoder @ 24000 Hz → [Resampler: 24k→48k] → Codec @ 48000 Hz
                     (1440 samples)   (2880 samples)
```

### Audio Codec Configuration

From documentation **Section 1: Audio Codec Configuration**:

- **Device Transmit**: Opus @ 16000 Hz, mono, 60ms frames
- **Server Transmit**: Opus at codec output rate (typically 24000 Hz)
- **Resampling**: Device auto-resamples if sample rates differ
- **AEC**: Server-side using playback timestamps when available

---

## OpenAI Realtime API Configuration

**Applies to:** OpenAI Realtime pipeline only

### OpenAI Realtime Capture (PCM)

When `USE_OPENAI_REALTIME` is enabled, capture uses raw PCM instead of Opus:

```
AudioService capture → PcmSampleCallback → processOpenAICapture(16kHz→24kHz) → OpenAIWebsocket::enqueuePcm()
```

- **Capture source:** `AudioService::captureTaskImpl()` via `setPcmSampleCallback`
- **Resampling:** `AudioService::processOpenAICapture()` (16kHz → 24kHz)
- **Transport:** PCM frames buffered and sent by `OpenAIWebsocket`

### OpenAI Realtime Playback (PCM)

OpenAI Realtime TTS playback uses PCM instead of Opus:

```
OpenAIWebsocket ring buffer → AudioService::startPcmPlayback() → pcmPlaybackTask → AudioCodec::write()
```

- **Source:** `OpenAIWebsocket` ring buffer (`popPcm`)
- **Playback task:** `AudioService::pcmPlaybackTaskImpl()` (10 ms chunks)
- **Priming:** `startPcmPlayback(prime_ms)` buffers before output

### Voice Activity Detection (VAD)

The OpenAI Realtime API uses server-side VAD to detect when the user starts and stops speaking. VAD parameters are configured via the `session.update` WebSocket message.

**Location:** `lib/services/OpenAIWebsocket.cpp`

#### VAD Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| **threshold** | 0.3 | VAD sensitivity (0.0-1.0)<br>Lower = more sensitive |
| **prefix_padding_ms** | 600 | Milliseconds of audio captured *before* VAD detects speech<br>Ensures beginning of words aren't cut off |
| **silence_duration_ms** | 500 | Milliseconds of silence before considering turn ended<br>Allows natural breathing/thinking pauses |

#### Parameter Details

**VAD Threshold (0.3):**
- Moderate sensitivity suitable for typical home/office environments
- Range: 0.0 (very sensitive) to 1.0 (less sensitive)
- Lower values detect quieter speech but may trigger on background noise
- Higher values reduce false triggers but may miss soft-spoken audio

**Prefix Padding (600ms):**
- Captures audio *before* the VAD algorithm confidently detects speech
- Critical for capturing soft consonants (H, S, F, W) at word beginnings
- Prevents missing first syllables (e.g., "Hello" vs "...ello")
- Value based on: speech ramp-up time (~100-200ms) + VAD detection latency (~50-150ms) + safety margin (~300ms)

**Silence Duration (500ms):**
- Amount of silence required before ending the user's speaking turn
- Allows natural pauses for breathing (400-600ms) and thinking (500-800ms)
- Prevents premature turn ending mid-sentence
- Lower values = faster response but may cut off pauses
- Higher values = more natural conversation but slower response

#### Runtime Configuration

VAD settings can be adjusted before connecting:

```cpp
// Configure for noisy environment
openai_client->setVadThreshold(0.5f);        // Less sensitive
openai_client->setVadPrefixPadding(700);     // More pre-capture
openai_client->setVadSilenceDuration(700);   // Longer silence threshold

// Connect with new settings
openai_client->connect();
```

**Common Configurations:**

| Use Case | Threshold | Prefix | Silence | Rationale |
|----------|-----------|--------|---------|-----------|
| **Quiet room** | 0.2 | 400ms | 400ms | More sensitive, faster response |
| **Normal (default)** | 0.3 | 600ms | 500ms | Balanced for most environments |
| **Noisy environment** | 0.5 | 700ms | 700ms | Less sensitive, avoids false triggers |
| **Slow/deliberate speech** | 0.3 | 600ms | 800ms | Allows longer thinking pauses |

**Location:** `lib/services/OpenAIWebsocket.h` (setter methods)

### Noise Reduction

OpenAI provides server-side noise reduction optimized for microphone placement.

**Setting:** `"near_field"`

**Options:**
- `near_field`: Microphone within 1 meter of speaker (Byte-90 configuration)
- `far_field`: Microphone distant from speaker (>1 meter)
- `null`: Disabled (rely solely on device-side processing)

**Location:** `lib/services/OpenAIApiClient.cpp`

The Byte-90 uses near-field mode since the microphone is integrated into the device.

### Session Configuration Logging

VAD and noise reduction settings are logged when the OpenAI session is established:

```
[OpenAIWebsocket] Session update VAD config: threshold=0.30, prefix_padding=600ms, silence_duration=500ms
```

This confirms the active configuration for each connection.

---

## Memory Management

### Memory Allocation Strategy

#### PSRAM (External RAM)

Used for **large, non-time-critical buffers**:

```cpp
_pcm_buffer = (int16_t*)heap_caps_malloc(max_frame_size * _channels * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
_opus_buffer = (uint8_t*)heap_caps_malloc(AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE,
                                          MALLOC_CAP_SPIRAM);
```

**Allocated in PSRAM:**
- PCM buffers (decoded audio)
- Opus buffers (encoded packets)
- Temporary resampling buffers

**Benefits:**
- Large capacity (~8MB PSRAM vs ~235KB internal)
- Reduces internal RAM pressure
- Suitable for non-realtime operations

#### Internal RAM

Used for **small, frequently-accessed data**:

```cpp
_resample_buffer = (int16_t*)malloc(_resample_buffer_size * _channels * sizeof(int16_t));
```

**Allocated in Internal RAM:**
- Resampling buffer (capture path)
- FreeRTOS queues and semaphores
- Task stacks
- State variables

**Benefits:**
- Faster access (< 100ns vs ~200ns)
- Lower latency
- Better for real-time operations

### Buffer Sizes

**Location:** `lib/audio/AudioService.cpp`

```cpp
// PCM buffer needs to handle both STT (16kHz) and TTS (codec output rate)
// Use decoder sample rate for buffer size since TTS playback may be higher
int max_frame_size = (decoder_sample_rate / 1000) * _frame_ms * 10;  // 10x for burst decoding
_pcm_buffer = (int16_t*)heap_caps_malloc(max_frame_size * _channels * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM);
_opus_buffer = (uint8_t*)heap_caps_malloc(AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE,
                                          MALLOC_CAP_SPIRAM);
```

**Calculations:**
- **max_frame_size**: (24000 / 1000) × 60 × 10 = 14400 samples
- **PCM buffer size**: 14400 × 1 × 2 = 28800 bytes (~28KB)
- **Opus buffer size**: 1024 bytes

### Queue Sizes

**Location:** Header constants

```cpp
#define AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE 1024
#define OPUS_DECODE_QUEUE_SIZE 20
#define PCM_OUTPUT_QUEUE_SIZE 2
#define SEND_QUEUE_SIZE 20
```

**Queue Allocations:**

| Queue | Size | Item Type | Total Memory |
|-------|------|-----------|--------------|
| **Opus Decode** | 20 | OpusPacket | ~40KB |
| **PCM Output** | 2 | PcmPacket | Minimal (queue control only, mostly unused) |
| **Send** | 20 | OpusPacket | ~40KB |
| **Timestamp** | 3 | uint32_t | Minimal |

### Memory Status Logging

**Location:** `lib/audio/AudioService.cpp`

```cpp
ESP_LOGI(TAG, "Memory after audio buffers: Internal=%dKB, PSRAM=%dKB",
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
```

**Typical Values:**
- **Internal RAM Free**: Varies by build
- **PSRAM Free**: Varies by build

---

## Performance Characteristics

### Encoding (Capture)

| Metric | Value | Notes |
|--------|-------|-------|
| **Input Format** | PCM 16-bit | Signed int16_t |
| **Input Size** | 1920 bytes | 960 samples × 2 bytes |
| **Output Format** | Opus | Compressed binary |
| **Output Size** | 60-100 bytes | Variable (DTX affects) |
| **Compression Ratio** | ~20:1 | Typical for voice |
| **Bitrate** | 8 kbps | 8000 bps |
| **CPU Usage** | Low | Complexity 0 |
| **Latency** | <10ms | Per 60ms frame |
| **Frame Duration** | 60ms | Fixed |

### Decoding (Playback)

| Metric | Value | Notes |
|--------|-------|-------|
| **Input Format** | Opus | Compressed binary |
| **Input Size** | Variable | Depends on content |
| **Output Format** | PCM 16-bit | Signed int16_t |
| **Output Size** | Variable | Depends on sample rate |
| **Sample Rate** | Codec output rate | Typically 24000 Hz |
| **CPU Usage** | Low | Opus is efficient |
| **Latency** | <10ms | Per frame |
| **FEC Support** | Yes | Packet loss concealment |

### Resampling

| Metric | Value | Notes |
|--------|-------|-------|
| **Algorithm** | SILK | From Skype codec |
| **Quality** | Professional | High quality |
| **CPU Usage** | Moderate | Depends on ratio |
| **Latency** | <1ms | Per frame |
| **Directions** | Both | Up and down |

### Bandwidth Usage

#### Capture (Device → Server)

**Without DTX:**
- **Frame Rate**: 1000ms / 60ms = 16.67 frames/sec
- **Packet Size**: ~80 bytes (average)
- **Bandwidth**: 16.67 × 80 × 8 = **10.67 kbps**

**With DTX (silence):**
- **Packet Size**: 2 bytes (silence)
- **Bandwidth**: 16.67 × 2 × 8 = **0.27 kbps**

#### Playback (Server → Device)

**TTS Streaming:**
- **Sample Rate**: Codec output rate (typically 24000 Hz)
- **Bitrate**: Variable (server determines)
- **Typical**: 16-32 kbps for music-quality TTS

### Task Priorities

**Location:** `lib/audio/AudioService.cpp`

| Task | Priority | Core | Purpose |
|------|----------|------|---------|
| **Capture** | 5 | 0 | Microphone input (time-critical) |
| **Opus Decode** | 2 | 1 | Decompress audio |
| **Audio Output** | 5 | 1 | Speaker output |

**Priority Scale**: 0 (lowest) to 25 (highest)

### Real-Time Performance

**Capture Path:**
```
60ms frame → Read (10ms) → Resample (1ms) → Encode (8ms) = ~19ms
Margin: 60ms - 19ms = 41ms (safe)
```

**Playback Path:**
```
Variable frame → Decode (5ms) → Resample (1ms) → Output (10ms) = ~16ms
Buffering: Opus decode queue 20 packets × 60ms = 1200ms, ring buffer 4 slots × 60ms = 240ms
```

---

## Code References

### Key Files

| File | Purpose | Lines |
|------|---------|-------|
| `lib/audio/AudioService.cpp` | Main audio orchestration | - |
| `lib/audio/OpusEncoder.cpp` | Opus encoding | - |
| `lib/audio/OpusDecoder.cpp` | Opus decoding | - |
| `lib/audio/OpusResampler.cpp` | Sample rate conversion | - |
| `lib/audio/AudioCodec.cpp` | ESP32-S3 I2S codec driver (ICS-43434 + MAX98357A) | - |

### Important Functions

#### Capture Path
- `AudioService::captureTaskImpl()` - `lib/audio/AudioService.cpp`
- `OpusEncoder::encode()` - `lib/audio/OpusEncoder.cpp`
- `OpusResampler::Process()` - `lib/audio/OpusResampler.cpp`

#### Playback Path
- `AudioService::opusDecodeTaskImpl()` - `lib/audio/AudioService.cpp`
- `OpusDecoder::decode()` - `lib/audio/OpusDecoder.cpp`
- `AudioService::audioOutputTaskImpl()` - `lib/audio/AudioService.cpp`

---

*Document generated from codebase analysis on 2026-01-10*
*Cross-referenced with lib/audio/ implementation*
