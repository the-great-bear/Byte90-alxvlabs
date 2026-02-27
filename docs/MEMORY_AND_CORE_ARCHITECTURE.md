# Memory and Core Architecture

**Document Version:** 2.1
**Last Updated:** 2026-01-10
**Hardware:** XIAO ESP32-S3 (Dual-Core, 8MB PSRAM)

---

## Table of Contents

1. [Overview](#overview)
2. [Embedded Systems Design Patterns](#embedded-systems-design-patterns)
3. [ESP32-S3 Hardware Architecture](#esp32-s3-hardware-architecture)
4. [Dual-Core Task Assignment](#dual-core-task-assignment)
5. [Memory Architecture](#memory-architecture)
6. [Task Priority Hierarchy](#task-priority-hierarchy)
7. [Why This Architecture Prevents Crashes](#why-this-architecture-prevents-crashes)
8. [Performance Characteristics](#performance-characteristics)
9. [Memory Allocation Strategy](#memory-allocation-strategy)
10. [Queue-Based Communication](#queue-based-communication)
11. [Summary](#summary)
---

## Overview

The Byte-90 firmware uses a **dual-core, priority-based task architecture** optimized for real-time audio streaming over WiFi. The system splits the audio pipeline across both CPU cores to maximize throughput while ensuring WiFi stability and preventing memory-related crashes.

### Key Design Principles:

1. **Core Separation**: Input path (Core 0) and output path (Core 1) run in parallel
2. **Priority-Based Scheduling**: WiFi and time-critical tasks have highest priority
3. **Memory Segregation**: Large buffers in PSRAM, queues in Internal RAM
4. **Ring Buffer Pattern**: Pre-allocated buffers eliminate dynamic allocation churn
5. **Non-Blocking Captures**: Audio capture never blocks on full queues
6. **Zero Memory Churn**: No malloc/free during audio streaming (ring buffer optimization)

---

## Embedded Systems Design Patterns

The Byte-90 firmware implements several well-established **embedded systems design patterns** optimized for resource-constrained, real-time environments like the ESP32-S3.

### 1. Producer-Consumer Pattern (Queue-Based)

**Pattern:** Multiple tasks communicate via FreeRTOS queues without direct coupling

**Implementation:** The Capture Task produces Opus packets and places them in a queue. The protocol layer consumes packets from the queue when ready. The queue acts as a buffer between producer and consumer, allowing them to run at different speeds without blocking each other.

**Benefits:**
- ✅ Decouples tasks (capture doesn't know about network protocol)
- ✅ Thread-safe by design (FreeRTOS handles synchronization)
- ✅ Natural buffering (queues absorb timing variations)
- ✅ Backpressure handling (non-blocking sends drop packets gracefully)

**Location:** `lib/audio/AudioService.cpp`

**Real-World Example:**
```cpp
// Producer: Capture task creates packet
OpusPacket packet;
packet.len = encoded_size;
xQueueSend(_send_queue, &packet, 0);  // Fire and forget

// Consumer: protocol layer reads when ready
OpusPacket packet;
if (xQueueReceive(_send_queue, &packet, 0) == pdTRUE) {
    audio_sink->sendAudio(buildStreamPacket(packet));
}
```

---

### 2. Ring Buffer Pattern (Zero-Churn Audio)

**Pattern:** Pre-allocated circular buffer for streaming audio data

**Implementation:** The decode task uses a ring buffer with 4 pre-allocated slots instead of dynamically allocating buffers for each audio packet. Each slot contains a PCM decode buffer (28,800 B) and a resample buffer (28,800 B). The decode task writes to slots sequentially (wrapping around), and the output task reads from slots sequentially, synchronized via semaphores.

**Benefits:**
- ✅ Zero memory allocation during audio streaming
- ✅ Zero memory churn (0 KB/s)
- ✅ Deterministic latency (no malloc timing variance)
- ✅ Cache-friendly memory reuse (same buffers reused continuously)
- ✅ Natural backpressure (ring full = decode blocks, providing flow control)

**Location:** `lib/audio/AudioService.h`, `lib/audio/AudioService.cpp`

**Ring Buffer Structure:**
```cpp
struct PcmRingBuffer {
    static const int RING_SIZE = 4;  // 240ms buffering (4 × 60ms)

    struct Slot {
        int16_t* pcm_data;      // Pre-allocated decode buffer
        int16_t* resample_data; // Pre-allocated resample buffer
        int samples;
        uint32_t timestamp;
        bool needs_resample;
        bool in_use;
    };

    Slot slots[RING_SIZE];
    volatile int write_idx;      // Decode writes here
    volatile int read_idx;       // Output reads here
    SemaphoreHandle_t full_sem;  // Counts full slots
    SemaphoreHandle_t empty_sem; // Counts empty slots
};
```

**Producer-Consumer Flow:**
```cpp
// Decode task (producer): Get empty slot
xSemaphoreTake(_ring_buffer.empty_sem, 100);  // Wait for empty
Slot* slot = &_ring_buffer.slots[_ring_buffer.write_idx];
opus_decode(..., slot->pcm_data, ...);  // Decode into pre-allocated buffer
_ring_buffer.write_idx = (write_idx + 1) % RING_SIZE;  // Advance
xSemaphoreGive(_ring_buffer.full_sem);  // Signal full

// Output task (consumer): Get full slot
xSemaphoreTake(_ring_buffer.full_sem, 100);  // Wait for full
Slot* slot = &_ring_buffer.slots[_ring_buffer.read_idx];
i2s_write(..., slot->pcm_data, ...);  // Use pre-allocated buffer
_ring_buffer.read_idx = (read_idx + 1) % RING_SIZE;  // Advance
xSemaphoreGive(_ring_buffer.empty_sem);  // Signal empty
```

**Why Ring Buffer vs Dynamic Allocation:**

Ring buffers provide deterministic performance by pre-allocating buffers at startup, eliminating runtime malloc/free operations. The trade-off is 225 KB of PSRAM (2.8% of 8MB) for complete elimination of memory allocation during audio streaming. This is a classic embedded systems optimization: **predictable memory usage** over dynamic flexibility.

**Ring Buffer Characteristics:**
- 0 malloc/free operations per second during streaming
- 0 KB/s memory churn
- Zero PSRAM fragmentation risk
- No malloc latency variance
- Excellent cache efficiency through buffer reuse

---

### 3. Priority-Based Preemptive Multitasking

**Pattern:** Real-Time Operating System (FreeRTOS) schedules tasks by priority

**Implementation:** Tasks are assigned priorities from 1 (lowest) to 23 (highest). WiFi has priority 23 for network stability. Capture has priority 5 for time-critical audio input. Output has priority 5 to avoid speaker glitches. Decode has priority 2 for CPU-intensive work. Main loop has priority 1 for best-effort UI updates.

**Benefits:**
- ✅ Deterministic timing for critical tasks
- ✅ WiFi never starved (highest priority)
- ✅ Capture completes within 60ms frame time
- ✅ Lower-priority tasks run when CPU available

**Location:** `lib/audio/AudioService.cpp`

**Why It Works:**
Higher priority tasks can **preempt** (interrupt) lower priority tasks. For example, when the decode task is running at priority 2 and a WiFi beacon arrives, the WiFi task at priority 23 immediately interrupts decode, services the beacon, and then decode resumes where it left off. This ensures WiFi never misses critical timing windows.

---

### 4. Memory Pool Pattern (Heap Segregation)

**Pattern:** Separate memory pools for different allocation types

**Implementation:** The system uses two distinct memory pools. Internal SRAM stores task stacks, queue structures, and small control buffers. PSRAM stores large audio buffers and ring buffer slots allocated at startup.

**Benefits:**
- ✅ Prevents fragmentation (large buffers isolated to PSRAM)
- ✅ Deterministic SRAM allocation (only small, fixed-size)
- ✅ Fast access for critical data (SRAM)
- ✅ Abundant space for buffers (8MB PSRAM)

**Location:** `lib/audio/AudioService.cpp`

**Code Example:**
```cpp
// PSRAM: Large audio buffers
_pcm_buffer = (int16_t*)heap_caps_malloc(
    max_frame_size * sizeof(int16_t),
    MALLOC_CAP_SPIRAM);
_opus_buffer = (uint8_t*)heap_caps_malloc(
    AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE,
    MALLOC_CAP_SPIRAM);

// SRAM: Small resample buffer (if needed)
_resample_buffer = (int16_t*)malloc(_resample_buffer_size * sizeof(int16_t));
```

---

### 5. Ring Buffer Slot Handoff (Zero-Copy)

**Pattern:** Reuse pre-allocated ring buffer slots without per-frame allocation

**Implementation:** The decode task writes directly into a ring buffer slot and signals availability via a semaphore. The output task consumes the slot and returns it to the pool. No malloc/free occurs during streaming.

**Benefits:**
- ✅ No memcpy() overhead (saves ~2ms per frame)
- ✅ No intermediate buffers needed (saves SRAM)
- ✅ Clear slot lifecycle (prevents memory leaks)

**Location:** `lib/audio/AudioService.cpp`

**Rule:** Producer fills slot, consumer releases slot. No ambiguity.

**Code Example:**
```cpp
// Decode task: Acquire empty slot and fill
xSemaphoreTake(_ring_buffer.empty_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.write_idx];
decode(opus, slot->pcm_data, ...);
_ring_buffer.write_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;
xSemaphoreGive(_ring_buffer.full_sem);  // Slot ready

// Output task: Consume slot and release
xSemaphoreTake(_ring_buffer.full_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.read_idx];
i2s_write(..., slot->pcm_data, ...);
_ring_buffer.read_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;
xSemaphoreGive(_ring_buffer.empty_sem);  // Slot reusable
```

---

### 6. Non-Blocking I/O with Timeout Pattern

**Pattern:** Tasks block on I/O but with configurable timeouts to prevent deadlock

**Implementation:** The capture task uses non-blocking queue sends (timeout 0) and I2S reads that block until audio is ready. The decode task uses `xQueueReceive()` with a 100ms timeout. The output task uses ring buffer semaphores with a 100ms timeout to avoid deadlock while still yielding CPU.

**Benefits:**
- ✅ Tasks yield CPU while waiting (not busy-looping)
- ✅ Watchdog doesn't trigger (tasks complete within timeout)
- ✅ Non-critical tasks can be interrupted
- ✅ Critical tasks never block indefinitely

**Location:** `lib/audio/AudioService.cpp`

**Code Examples:**
```cpp
// Capture: Non-blocking send (critical path)
if (xQueueSend(_send_queue, &packet, 0) != pdTRUE) {
//                                     ^^^ timeout = 0
    ESP_LOGW(TAG, "Queue full, dropping packet");
}

// I2S read: Blocking until DMA fills buffer
int samples_read = _codec->read(_pcm_buffer, samples_to_read);
                         bytes_to_read, &bytes_read,
                         pdMS_TO_TICKS(100));
//                       ^^^^^^^^^^^^^^^^^^^ 100ms timeout
```

---

### 7. Dual-Core Pipeline Parallelism

**Pattern:** Split processing pipeline across CPU cores for parallel execution

**Implementation:** Core 0 handles the input pipeline: microphone to capture to encode to network transmission. Core 1 handles the output pipeline: network reception to decode to resample to speaker. Both pipelines run simultaneously on their respective cores.

**Benefits:**
- ✅ Input and output run simultaneously (no blocking)
- ✅ Better CPU utilization (both cores active)
- ✅ Cache efficiency (pipeline stages share core's cache)
- ✅ Reduced contention (fewer tasks per core)

**Location:** `lib/audio/AudioService.cpp`

**Why This Works:**
At any given time, Core 0 is encoding the current audio frame while Core 1 is decoding and playing a previous frame. This parallel processing doubles the effective throughput compared to running all tasks on a single core.

---

### 8. Fail-Safe Pattern (Graceful Degradation)

**Pattern:** System degrades gracefully under overload instead of crashing

**Implementation:** When the network is slow and the send queue fills up, a traditional approach would block until space is available, triggering watchdog timeout and crash. The fail-safe approach drops the packet, logs a warning, and continues running. The user may hear a slight audio glitch but the device stays operational.

**Benefits:**
- ✅ No catastrophic failures (device stays running)
- ✅ User experiences minor glitch vs total crash
- ✅ System self-recovers when load decreases

**Location:** `lib/audio/AudioService.cpp`

**Code Example:**
```cpp
if (xQueueSend(_send_queue, &packet, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Send queue full, dropping packet");
    // Device continues running, slight audio gap
} else {
    // Normal operation
}
```

---

### 9. System Health Monitoring

**Pattern:** Continuous monitoring of critical system parameters

**Implementation:** The system continuously monitors buffer fill levels, memory usage, state durations, and network health to detect issues before they cause failures.

**Benefits:**
- ✅ Detects issues proactively (before failure occurs)
- ✅ Automatic recovery from transient problems
- ✅ Comprehensive logging for diagnostics

#### TTS Timeout Watchdog

The system monitors time spent in SPEAKING state. If a TTS response exceeds 30 seconds, the system automatically:
- Cancels the in-progress response
- Clears audio buffers
- Returns to LISTENING state
- Logs the timeout event

**Location:** `src/ApplicationAudio.cpp`

**Timeout:** 30 seconds (covers longest expected TTS responses)

**Benefit:** Prevents "stuck in speaking" state when API hangs or responses are cancelled.

#### Send Queue Buffer Monitoring

Real-time monitoring of the send queue (shared across all pipelines) with three-tier warning system:

| Fill Level | Severity | Action | Log Interval |
|------------|----------|--------|--------------|
| >90% | CRITICAL | Network slow warning | Every 2s |
| >70% | WARNING | Elevated usage | Every 5s |
| >50% | INFO | Moderate usage | Every 10s |

**Location:** `lib/audio/AudioService.cpp`

**Benefit:** Early detection of network slowness before audio drops occur.

#### Memory Usage Tracking

System logs detailed memory status after buffer allocation:
- Internal RAM: used/total/percentage free
- PSRAM: used/total/percentage free
- Per-component allocation sizes

**Locations:** `lib/audio/AudioService.cpp`, `lib/services/OpenAIWebsocket.cpp`

**Benefit:** Visibility into memory consumption, early leak detection.

#### State Transition Logging

Every system state transition is logged with context:
- Previous state → New state
- WiFi connection status
- Free heap (Internal RAM)
- Free PSRAM

**Location:** `lib/system/SystemState.cpp`

**Example:**
```
[SystemState] State: LISTENING -> SPEAKING | WiFi: ON | Heap: 145KB | PSRAM: 7234KB
```

**Benefit:** Complete visibility for debugging state-related issues.

---

### 10. State Machine Pattern

**Pattern:** Device behavior controlled by explicit state machine

**Implementation:** The device transitions through well-defined states: IDLE to CONNECTING to LISTENING to SPEAKING and back to IDLE. Button press triggers IDLE to CONNECTING. Server hello completes CONNECTING to IDLE. Starting listening moves IDLE to LISTENING. TTS start transitions LISTENING to SPEAKING. TTS stop returns to IDLE (or LISTENING in auto mode).

**Benefits:**
- ✅ Clear, predictable behavior
- ✅ Easy to debug (log state transitions)
- ✅ Prevents invalid operations (can't listen while speaking)

**Location:** `lib/system/system_state.h`, `src/application.cpp:669-717`

**Code Example:**
```cpp
void Application::onButtonClick() {
    // State-based decision tree
    if (_state_manager->getState() == SYSTEM_STATE_SPEAKING) {
        // Interrupt TTS
        sendAbort();
    } else if (_audio->isListening()) {
        // Stop listening
        stopListening();
    } else {
        // Start listening
        startListening();
    }
}
```

---

### 11. Interrupt-Driven I/O Pattern

**Pattern:** Hardware events trigger interrupts, wake tasks via queues

**Implementation:** When I2S hardware fills the DMA buffer, it triggers an interrupt. FreeRTOS wakes the capture task blocked on i2s_read(). The capture task resumes, processes the audio frame, and then blocks on the next i2s_read() call, yielding CPU until the next interrupt.

**Benefits:**
- ✅ No busy-waiting (CPU sleeps until data ready)
- ✅ Immediate response to hardware events
- ✅ Low power consumption (CPU idle when no audio)

**Location:** `lib/audio/AudioService.cpp` (i2s_read blocks until interrupt)

**How It Works:**
```cpp
// Capture task blocks here (90% of time)
int samples_read = _codec->read(_pcm_buffer, samples_to_read);
//                          ^^^^ Blocks until I2S DMA fills buffer
//                               (triggered by hardware interrupt)

// Task wakes when data ready, processes immediately
```

---

### 12. Resource Acquisition Is Initialization (RAII) Pattern

**Pattern:** Resources tied to object lifetime (C++ destructors clean up)

**Implementation:**
```cpp
class AudioService {
    AudioService() {
        _opus_buffer = new uint8_t[MAX_SIZE];
        _encoder = new OpusEncoder();
    }

    ~AudioService() {
        delete _encoder;
        delete[] _opus_buffer;
        // Automatic cleanup when object destroyed
    }
};
```

**Benefits:**
- ✅ No manual cleanup needed
- ✅ Exception-safe (destructor always called)
- ✅ Prevents resource leaks

**Location:** `lib/audio/AudioService.cpp`

---

### Design Pattern Summary

| Pattern | Purpose | Benefit |
|---------|---------|---------|
| Producer-Consumer | Task decoupling | Thread-safe, buffered communication |
| Ring Buffer | Zero-churn audio | Eliminates 781 KB/s malloc/free overhead |
| Priority Scheduling | Real-time guarantees | Deterministic timing for critical tasks |
| Memory Pool | Heap segregation | Prevents fragmentation, optimizes access |
| Ownership Transfer | Zero-copy | Eliminates memcpy overhead |
| Non-Blocking I/O | Prevent deadlock | Watchdog-safe, graceful degradation |
| Dual-Core Pipeline | Parallel processing | 2× throughput, better utilization |
| Fail-Safe | Graceful degradation | System stays running under overload |
| System Health Monitoring | Proactive detection | Automatic recovery, diagnostic visibility |
| State Machine | Behavior control | Predictable, debuggable logic |
| Interrupt-Driven I/O | Event-based | Low latency, low power |
| RAII | Resource management | Automatic cleanup, leak-proof |

**These patterns work together** to create a robust, real-time embedded system that can run for days without crashes, memory leaks, or audio glitches.

---

## ESP32-S3 Hardware Architecture

### CPU Cores

The XIAO ESP32-S3 has two Xtensa LX7 CPU cores:

- **Core 0**: Typically runs WiFi stack and system tasks (ESP-IDF default)
- **Core 1**: Available for application tasks

**Clock Speed:** 240 MHz (both cores)

### Memory Architecture

#### Internal SRAM
- **Size:** ~320 KB available after boot
- **Speed:** Single-cycle access
- **Location:** On-chip, tightly coupled to CPU
- **Best for:** Stacks, queues, frequently accessed small data
- **Limitation:** Fragmentation-prone if mismanaged

#### PSRAM (Pseudo-Static RAM)
- **Size:** 8 MB OPI PSRAM (Octal SPI)
- **Speed:** ~80 MB/s bandwidth (2× faster than QSPI PSRAM)
- **Location:** External chip via QSPI/OPI interface
- **Best for:** Large audio buffers, temporary allocations
- **Advantage:** Abundant, fragmentation-tolerant

---

## Dual-Core Task Assignment

### Optimized Architecture (Current)

The firmware splits the audio pipeline into **input path (Core 0)** and **output path (Core 1)** for optimal load distribution and WiFi stability.

```
┌─────────────────────────────────────────────────────────────┐
│                         CORE 0                              │
│                   (Network + Input Path)                    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  WiFi Stack (Priority ~23) ─────────────────────┐          │
│  TCP/IP lwIP (Priority ~18) ────────────────┐   │          │
│                                              │   │          │
│  ┌────────────────────────────────────────┐ │   │          │
│  │   Capture Task (Priority 5, Core 0)    │ │   │          │
│  │                                        │ │   │          │
│  │   Microphone (I2S)                     │ │   │          │
│  │         ↓                              │ │   │          │
│  │   Read PCM (blocks in i2s_read)       │ │   │          │
│  │         ↓                              │ │   │          │
│  │   Resample (if needed)                 │ │   │          │
│  │         ↓                              │ │   │          │
│  │   Opus Encode (complexity 0, DTX)      │ │   │          │
│  │         ↓                              │ │   │          │
│  │   xQueueSend(send_queue) [non-block]  │ │   │          │
│  │         ↓                              │ │   │          │
│  │   Protocol Send ──────────────────────┼─┼───┘          │
│  │                                        │ │              │
│  └────────────────────────────────────────┘ │              │
│                                             │              │
│  WiFi can preempt capture (23 > 5) ────────┘              │
│                                                             │
└─────────────────────────────────────────────────────────────┘

                            ↓ Queue Transfer
                    (Cross-core, ~1-2μs overhead)
                            ↓

┌─────────────────────────────────────────────────────────────┐
│                         CORE 1                              │
│                      (Output Path)                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌────────────────────────────────────────────────────┐    │
│  │   Decode Task (Priority 2, Core 1)                 │    │
│  │                                                    │    │
│  │   xQueueReceive(decode_queue) [100ms timeout]     │    │
│  │         ↓                                          │    │
│  │   Opus Decode → PCM (ring buffer slot)            │    │
│  │         ↓                                          │    │
│  │   Resample (if decoder rate ≠ codec rate)         │    │
│  │         ↓                                          │    │
│  │   xSemaphoreGive(full_sem) [slot ready]           │    │
│  │                                                    │    │
│  └────────────────────────────────────────────────────┘    │
│                            ↓                                │
│  ┌────────────────────────────────────────────────────┐    │
│  │   Output Task (Priority 5, Core 1)                 │    │
│  │                                                    │    │
│  │   xSemaphoreTake(full_sem) [100ms timeout]        │    │
│  │         ↓                                          │    │
│  │   I2S Write (speaker)                              │    │
│  │         ↓                                          │    │
│  │   xSemaphoreGive(empty_sem) [slot reusable]        │    │
│  │                                                    │    │
│  └────────────────────────────────────────────────────┘    │
│  ┌────────────────────────────────────────────────────┐    │
│  │   PCM Playback Task (Priority 5, Core 1)           │    │
│  │   [OpenAI Realtime only]                           │    │
│  │   OpenAI ring buffer → I2S write                   │    │
│  └────────────────────────────────────────────────────┘    │
│                                                             │
│  Main Application Loop (Priority 1, Core 1)                │
│  - Button handling                                         │
│  - State management                                        │
│  - UI updates                                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Why This Split?

**Core 0 (Network + Capture):**
- ✅ Capture task close to WiFi → minimal latency for STT streaming
- ✅ Microphone samples encoded and sent immediately (real-time critical)
- ✅ WiFi can preempt capture when needed (priority 23 > 5)

**Core 1 (Decode + Output):**
- ✅ Decode and output stay together → PCM data stays in Core 1 cache
- ✅ Natural buffering (queue + ring buffer) absorbs network jitter
- ✅ Parallel processing with input path
- ✅ Core 0 has more breathing room for WiFi stability

**Location References:**
- Capture task: `lib/audio/AudioService.cpp` (Core 0)
- Decode task: `lib/audio/AudioService.cpp` (Core 1)
- Output task: `lib/audio/AudioService.cpp` (Core 1)

---

## Memory Architecture

### Internal SRAM Allocation

**Total Available:** ~320 KB after ESP-IDF boot

| Component | Size | Purpose | Location |
|-----------|------|---------|----------|
| Capture Task Stack | 24,576 B (24 KB) | Opus encoding workspace | `lib/audio/AudioService.cpp` |
| Decode Task Stack | 24,576 B (24 KB) | Opus decoding workspace | `lib/audio/AudioService.cpp` |
| Output Task Stack | 4,096 B (4 KB) | I2S write operations | `lib/audio/AudioService.cpp` |
| PCM Playback Stack | 4,096 B (4 KB) | OpenAI realtime playback (optional) | `lib/audio/AudioService.cpp` |
| Opus Queue Storage | 41,280 B (40.3 KB) | Send + Decode queues (2 × 20 × 1,032 B) | `lib/audio/AudioService.h` |
| PCM Output Queue | 24 B | 2 items × 12 B | `lib/audio/AudioService.h` |
| Timestamp Queue | 12 B | 3 items × 4 bytes | `lib/audio/AudioService.h` |
| Input Resample Buffer | 1,920 B (1.9 KB) | 48kHz → 16kHz capture (if needed) | `lib/audio/AudioService.cpp` |
| OpenAI Resample Buffer | 480 B | 10 ms @ 24kHz (OpenAI, optional) | `lib/audio/AudioService.cpp` |
| OpenAI Resample Output | 480 B | 10 ms @ 24kHz (OpenAI) | `src/ApplicationAudio.cpp` |
| OpenAI Preroll Input | 320 B | 10 ms @ 16kHz (OpenAI) | `src/ApplicationAudio.cpp` |
| OpenAI Preroll Buffer | 25,600 B (25.0 KB) | 800 ms @ 16kHz (OpenAI) | `src/ApplicationAudio.cpp` |

**Totals (SRAM, audio tasks):**
- Opus and OpenAI Realtime paths are build-time exclusive (`USE_OPENAI_REALTIME`).
- Opus pipeline w/ 48kHz → 16kHz resample: 96,484 B (94.2 KB)
- OpenAI Realtime pipeline: 31,576 B (30.8 KB) for OpenAI-specific buffers + 4,096 B PCM playback stack

### PSRAM Allocation

**Total Available:** 8 MB OPI PSRAM

| Component | Size | Allocation | Lifetime |
|-----------|------|------------|----------|
| Ring Buffer (4 slots) | 230,400 B (225.0 KB) | Pre-allocated at startup | Persistent during session |
| - PCM buffers (4×) | 4 × 28,800 B | 4 slots × PCM buffer | Reused continuously |
| - Resample buffers (4×) | 4 × 28,800 B | 4 slots × resample buffer | Reused continuously |
| PCM Capture Buffer | 28,800 B (28.1 KB) | Max frame buffer (10x @ 24kHz) | Allocated once, reused |
| Opus Encode Buffer | 1,024 B (1.0 KB) | `_opus_buffer` in PSRAM | Persistent during session |
| OpenAI PCM Ring Buffer | 2,097,152 B (2048 KB) | OpenAI playback (optional) | Persistent during session |
| OpenAI TX Ring Buffer | 32,768 B (32 KB) | OpenAI capture (optional) | Persistent during session |

**Totals (PSRAM, audio buffers):**
- AudioService (ring buffer + PCM buffer + Opus buffer): 260,224 B (254.1 KB)
- OpenAI buffers add: 2,129,920 B (2080 KB)

### Why This Split Prevents Crashes

#### Problem: SRAM Fragmentation
Without PSRAM, allocating 28.8 KB buffers every 60ms would cause:

```
[SRAM Fragmentation Example]
Initial:  [████████████████████] 320KB free

After 10 minutes:
[█ 80KB ██ 60KB █████ 100KB ██ 40KB █ 30KB] Total: 310KB free
          ↑                ↑
    Largest blocks: 100KB and 80KB

malloc(110KB) → FAILS despite 310KB total free → CRASH
```

**Solution:** Large buffers go to PSRAM where fragmentation is tolerable (8 MB available).

#### Problem: Stack Overflow
Each task needs workspace for function calls and local variables:

**Capture Task Stack (24 KB):**
- I2S driver calls: ~200 bytes
- Opus encoder workspace: ~8 KB
- Resampler buffers: ~6 KB
- Function call overhead: ~500 bytes
- Safety margin: 2× = **24 KB total**

**Why SRAM?** FreeRTOS monitors stack watermarks. Stack overflow in SRAM triggers watchdog (controlled reboot) instead of silent PSRAM corruption.

**Location:** `lib/audio/AudioService.cpp`

---

## Task Priority Hierarchy

### Complete Priority Map

```
Core 0 Tasks:
├─ WiFi Driver (Priority ~23) ───────────── ESP-IDF managed, highest
├─ lwIP TCP/IP (Priority ~18) ─────────── ESP-IDF managed
├─ Capture Task (Priority 5) ──────────── Application, highest audio priority
└─ System Tasks (Priority 1 or lower) ──── ESP-IDF managed

Core 1 Tasks:
├─ PCM Playback Task (Priority 5) ───────── OpenAI Realtime only
├─ Output Task (Priority 5) ───────────── Time-critical I2S writes
├─ Decode Task (Priority 2) ───────────── CPU-intensive Opus decoding
└─ Main Loop (Priority 1) ─────────────── Button/UI, lowest priority
```

### Priority Rationale

**Capture (Priority 5):**
- **Why:** Must never miss microphone samples (unrecoverable data loss)
- **Timing:** Must complete within 60ms frame time
- **Blocking:** Spends 90% of time blocked in `i2s_read()` (100ms timeout)
- **Preemption:** WiFi (priority 23) can interrupt when needed

**WiFi/TCP (Priority 18-23):**
- **Why:** Must service WiFi beacons every 100ms to maintain connection
- **Timing:** Critical for network stability
- **Preemption:** Can interrupt all audio tasks

**Output (Priority 5):**
- **Why:** Speaker buffer underruns cause audible glitches
- **Timing:** Must write to I2S within 100-200ms buffer window
- **Blocking:** Waits on ring buffer semaphores with 100ms timeout

**Decode (Priority 2):**
- **Why:** CPU-intensive, can lag slightly without immediate glitches
- **Buffering:** Queue provides ~1200ms tolerance
- **Preemption:** Output task (priority 5) can interrupt

**Main Loop (Priority 1):**
- **Why:** UI and button handling are "best effort"
- **Timing:** Non-critical, runs when CPU available

**Location:** `lib/audio/AudioService.cpp`

---

## Why This Architecture Prevents Crashes

### Crash Scenario 1: SRAM Exhaustion

**Without PSRAM allocation:**

```
[SRAM: large PCM buffers + ring buffer slots]
[SRAM: stacks + queues]
→ fragmentation over time → large allocation fails → crash
```

**With PSRAM allocation (current):**

```
[SRAM: stacks + queues only]
[PSRAM: ring buffer slots + audio working buffers]
→ fragmentation risk minimized → stable long runs
```

### Crash Scenario 2: WiFi Beacon Miss

**Without proper priorities:**

```
1. Capture task (priority 5) starts encoding (5-10ms)
2. WiFi beacon arrives (every 100ms)
3. WiFi task (priority 23) preempts capture → SUCCESS
4. Capture resumes after WiFi completes
5. No crash
```

**If capture had priority 25 (higher than WiFi):**

```
1. Capture task (priority 25) starts encoding
2. WiFi beacon arrives
3. WiFi task (priority 23) CANNOT preempt
4. WiFi beacon missed → Connection drops → CRASH
```

**Solution:** WiFi priority (23) > Capture priority (8) ensures network stability.

### Crash Scenario 3: Watchdog Timeout

**Without non-blocking captures:**

```
1. Network is slow, send queue fills up
2. Capture task calls xQueueSend(_send_queue, &packet, portMAX_DELAY)
3. Capture task BLOCKS waiting for queue space
4. Watchdog timer: "Capture task blocked for >1 second"
5. Watchdog triggers → CRASH
```

**With non-blocking captures (current):**

```cpp
// lib/audio/AudioService.cpp
if (xQueueSend(_send_queue, &packet, 0) != pdTRUE) {
//                                     ^^^ timeout = 0 (non-blocking)
    ESP_LOGW(TAG, "Send queue full, dropping packet");
}
```

```
1. Send queue full
2. xQueueSend() returns immediately (pdFALSE)
3. Packet dropped gracefully
4. Capture task completes in <100ms
5. Watchdog happy, no crash
```

### Crash Scenario 4: Memory Leak

**Without clear slot lifecycle:**

```
1. Decode task writes into shared buffer without ownership rules
2. Output task reads while decode task overwrites
3. Data races → audio corruption or crashes
```

**With clear slot lifecycle (current):**

```cpp
// Decode task: Fill slot and signal ready
xSemaphoreTake(_ring_buffer.empty_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.write_idx];
decode(opus, slot->pcm_data, ...);
xSemaphoreGive(_ring_buffer.full_sem);

// Output task: Consume slot and release
xSemaphoreTake(_ring_buffer.full_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.read_idx];
_codec->write(slot->pcm_data, slot->samples);
xSemaphoreGive(_ring_buffer.empty_sem);
```

**Rule:** Slot ownership moves via semaphores; no malloc/free during streaming.

---

## Performance Characteristics

### CPU Utilization (During Full-Duplex Conversation)

**Core 0 (Network + Input):**
- WiFi stack: ~40-50% (receiving TTS + sending STT)
- Capture encode: ~10-15% (every 60ms, ~5-10ms CPU time)
- TCP/IP overhead: ~5-10%
- **Total Core 0:** ~60-70% utilization

**Core 1 (Output):**
- Decode: ~15-20% (during TTS playback)
- Output: ~5-10% (I2S writes)
- Main loop: ~5% (button/UI)
- **Total Core 1:** ~25-35% utilization

**Load Distribution:** Balanced across both cores for optimal performance

### Latency Measurements

| Path | Latency | Critical? |
|------|---------|-----------|
| Microphone → Capture task | ~60ms (I2S buffer) | No (buffered) |
| Capture → Encode | <5ms | No |
| Encode → AudioPacketSink send | <10ms | Yes (STT real-time) |
| protocol receive → Decode queue | <2ms | No |
| Decode queue → PCM decode | Variable (up to ~1200ms buffered) | No |
| PCM → I2S output | <50ms | Yes (avoid glitches) |

**Total Input Latency (Mic → Server):** ~75ms
**Total Output Latency (Server → Speaker):** ~150ms (acceptable for TTS)

### Memory Usage

**Peak Memory Usage (Full-Duplex):**
- Internal SRAM: ~60-100 KB (queues + stacks + control)
- PSRAM: ~200-250 KB (ring buffer + audio working buffers)

**Headroom:**
- SRAM: ~220-260 KB available (varies by build)
- PSRAM: ~7.75-7.8 MB available (varies by build)

**Sustainability:** Can run for days without memory leaks or fragmentation issues.

---

## Memory Allocation Strategy

### Allocation Rules

**Use Internal SRAM for:**
1. ✅ Task stacks (watchdog protection)
2. ✅ Queue control structures (atomic operations)
3. ✅ Frequently accessed small buffers (<2 KB)
4. ✅ Variables requiring deterministic access

**Use PSRAM for:**
1. ✅ Large audio buffers (>2 KB)
2. ✅ Ring buffer slots and working buffers (persistent during session)
3. ✅ Resample buffers (persistent but large)
4. ✅ Any buffer that causes fragmentation if in SRAM

### Allocation Examples

**SRAM Allocation (queues + mutexes):**
```cpp
_opus_decode_queue = xQueueCreate(OPUS_DECODE_QUEUE_SIZE, sizeof(OpusPacket));
_send_queue = xQueueCreate(SEND_QUEUE_SIZE, sizeof(OpusPacket));
_timestamp_queue = xQueueCreate(TIMESTAMP_QUEUE_SIZE, sizeof(uint32_t));
_state_mutex = xSemaphoreCreateMutex();
```

**PSRAM Allocation (audio buffers):**
```cpp
// lib/audio/AudioService.cpp
// PCM ring buffer slots + working buffers
int16_t* pcm_data = (int16_t*)heap_caps_malloc(
    max_decode_samples * _channels * sizeof(int16_t),
    MALLOC_CAP_SPIRAM);  // Explicit PSRAM
```

---

## Queue-Based Communication

### Queue Architecture

The system uses **3 FreeRTOS queues** for inter-task communication plus a
ring buffer with semaphores for PCM handoff:

```
┌─────────────────────────────────────────────────────────────┐
│                      Queue Architecture                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. Send Queue (_send_queue)                               │
│     - Size: 20 items × sizeof(OpusPacket)                  │
│     - Producer: Capture task (Core 0)                      │
│     - Consumer: protocol layer (Core 0)                    │
│     - Content: Opus packets (data copied)                  │
│     - Timeout: 0 (non-blocking send)                       │
│                                                             │
│  2. Timestamp Queue (_timestamp_queue)                     │
│     - Size: 3 items × sizeof(uint32_t)                     │
│     - Producer: Audio output task (Core 1)                 │
│     - Consumer: Capture task (Core 0)                      │
│     - Content: Timestamps for AEC                          │
│     - Timeout: 0 (non-blocking)                            │
│                                                             │
│  3. Decode Queue (_opus_decode_queue)                      │
│     - Size: 20 items × sizeof(OpusPacket)                  │
│     - Producer: protocol layer (Core 0)                    │
│     - Consumer: Decode task (Core 1) ← CROSSES CORES       │
│     - Content: Opus packets from server (data copied)      │
│     - Timeout: 100ms (blocking receive)                    │
│                                                             │
│  Ring Buffer Semaphores (_ring_buffer.full_sem/empty_sem)  │
│     - Producer: Decode task (Core 1)                       │
│     - Consumer: Output task (Core 1)                       │
│     - Content: Pre-allocated PCM slots                     │
│     - Timeout: 100ms (blocking take)                       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Queue Memory Ownership Rules

**Send Queue (Opus Encode → Protocol):**
```cpp
// Capture task creates local packet
OpusPacket packet;
memcpy(packet.data, _opus_buffer, encoded_size);
packet.len = encoded_size;

// Copy to queue (data is copied, not pointer)
xQueueSend(_send_queue, &packet, 0);

// Safe to reuse _opus_buffer immediately
```

**Decode Queue (Protocol → Opus Decode):**
```cpp
// Protocol creates local packet
OpusPacket packet;
memcpy(packet.data, data, len);
packet.len = len;

// Copy to queue (data is copied)
xQueueSend(_opus_decode_queue, &packet, 0);

// Decode task receives copy
OpusPacket opusPacket;
xQueueReceive(_opus_decode_queue, &opusPacket, 100);
```

**PCM Ring Buffer (Decode → Output) - SLOT HANDOFF:**
```cpp
// Decode task: Fill slot and signal
xSemaphoreTake(_ring_buffer.empty_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.write_idx];
decode(opus, slot->pcm_data, ...);
_ring_buffer.write_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;
xSemaphoreGive(_ring_buffer.full_sem);

// Output task: Consume slot and release
xSemaphoreTake(_ring_buffer.full_sem, pdMS_TO_TICKS(100));
PcmRingBuffer::Slot* slot = &_ring_buffer.slots[_ring_buffer.read_idx];
_codec->write(slot->pcm_data, slot->samples);
_ring_buffer.read_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;
xSemaphoreGive(_ring_buffer.empty_sem);
```

**Rule:** All queues copy data; ring buffer slots are reused (no malloc/free).

**Location:** `lib/audio/AudioService.cpp`

---

## Summary

The Byte-90 firmware implements a **dual-core, priority-based, zero-churn architecture** that:

1. **Prevents crashes** through memory segregation (SRAM vs PSRAM) and ring buffer pattern
2. **Ensures WiFi stability** by giving network tasks priority over audio
3. **Maximizes throughput** by splitting input/output paths across cores
4. **Eliminates memory churn** through pre-allocated ring buffers
5. **Avoids memory leaks** through clear ownership rules and zero dynamic allocation
6. **Supports real-time audio** with deterministic task scheduling and fixed-time buffer access

**Key Architecture Metrics:**
- **SRAM usage:** 94 KB (~29% of 320 KB available)
- **PSRAM static:** 237 KB (~2.9% of 8 MB available)
- **Memory churn:** 0 KB/s during audio streaming
- **Malloc operations:** 0/sec during streaming
- **Core 0 utilization:** ~60-70% (WiFi + Capture)
- **Core 1 utilization:** ~15-25% (Decode + Output)
- **Input latency:** ~75ms (Mic → Server)
- **Output latency:** ~150ms (Server → Speaker)

This architecture enables **stable, hours-long conversations** without memory exhaustion, fragmentation, WiFi disconnections, or audio glitches. The ring buffer pattern ensures **zero dynamic allocation** during audio streaming, making the system highly predictable and deterministic.
