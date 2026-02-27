/**
 * AudioService.h
 *
 * Declarations for AudioService.
 */

#pragma once

// Project includes
#include "AudioCodec.h"
#include "DeviceConfig.h"
#include "OpusDecoder.h"
#include "OpusEncoder.h"
#include "OpusResampler.h"

// Arduino/ESP32 includes
#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>

// Standard includes
#include <vector>

// Constants

/**
 * Maximum size of an Opus packet in bytes
 */
#define AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE 1024

// Type definitions

/**
 * @brief rate.
 * Audio stream packet structure
 *
 * Interface between audio service and network protocols for
 * transmitting encoded audio data.
 */
struct AudioStreamPacket {
    int sample_rate = 0;           // Audio sample rate (Hz)
    int frame_duration = 0;        // Frame duration (ms)
    uint32_t timestamp = 0;        // Timestamp (ms, for AEC)
    std::vector<uint8_t> payload;  // Opus-encoded audio data
};

/**
 * AudioPacketSink - Interface for sending encoded audio packets.
 */
class AudioPacketSink {
public:
    virtual ~AudioPacketSink() = default;

    /**
     * @brief Check if audio channel is open
     *
     * @return true if open, false otherwise
     */
    virtual bool isAudioChannelOpened() const = 0;

    /**
     * @brief Send audio packet to protocol
     *
     * @param packet Audio packet (ownership transferred)
     * @return true on success, false on failure
     */
    virtual bool sendAudio(AudioStreamPacket* packet) = 0;
};

/**
 * Ring buffer for PCM decode/resample buffers
 *
 * Eliminates 781 KB/s memory churn by pre-allocating 4 slots
 * providing 240ms of buffering (4 × 60ms frames).
 */
struct PcmRingBuffer {
    static const int RING_SIZE = 6;  // 6 slots = 360ms buffering

    /**
 * @brief buffer.
     * Ring buffer slot containing decoded and resampled PCM data
     */
    struct Slot {
        int16_t* pcm_data;      // Decode output buffer (28.8 KB @ 24kHz)
        int16_t* resample_data; // Resample output buffer (18.8 KB @ 16kHz)
        int samples;            // Number of samples in buffer
        uint32_t timestamp;     // AEC timestamp
        bool needs_resample;    // Flag indicating if resampling was applied
        bool in_use;            // Slot occupancy flag
    };

    Slot slots[RING_SIZE];       // Pre-allocated slots
    volatile int write_idx;       // Decode task writes here
    volatile int read_idx;        // Output task reads here
    SemaphoreHandle_t full_sem;   // Counts full slots (decode waits if full)
    SemaphoreHandle_t empty_sem;  // Counts empty slots (output waits if empty)
};

/**
 * @brief void.
 * Audio packet callback function type
 *
 * Called when encoded audio packets are ready for transmission.
 *
 * @param data Pointer to encoded audio packet data
 * @param len Length of packet in bytes
 * @param user_data User-defined data pointer
 */
typedef void (*AudioPacketCallback)(const uint8_t* data, int len, void* user_data);

// Forward declarations
/**
 * @brief AudioService.
 */
class AudioService;

/**
 * AudioService - Audio streaming service with Opus encoding/decoding
 *
 * Features:
 * - Real-time audio capture with Opus encoding
 * - Real-time audio playback with Opus decoding
 * - Multi-task architecture (3 FreeRTOS tasks)
 * - Ring buffer for zero-copy playback
 * - Automatic sample rate conversion
 * - Server-side AEC timestamp support
 *
 * Architecture:
 * - Capture task (priority 8): Reads from microphone, encodes to Opus
 * - Decode task (priority 2): Decodes Opus packets to PCM
 * - Output task (priority 4): Writes PCM to speaker via I2S
 * - Ring buffer eliminates 781 KB/s memory allocation churn
 * - Queue sizes optimized for 1200ms network jitter tolerance
 *
 * Memory optimization:
 * - Pre-allocated ring buffer (4 slots × 47.6 KB = 190 KB)
 * - Reduced queue sizes save 40 KB SRAM
 * - Total buffering: 1200ms for decode + 240ms for output
 *
 * Hardware:
 * - ESP32-S3 I2S audio codec
 * - ICS-43434 microphone
 * - MAX98357A speaker amplifier
 */
class AudioService {
public:
    /**
     * @brief Construct audio service instance
     *
     * @param codec Pointer to initialized AudioCodec
     * @param sample_rate Opus sample rate in Hz (8000, 12000, 16000, 24000, 48000)
     * @param channels Number of channels (1=mono, 2=stereo)
     * @param frame_ms Frame duration in milliseconds (10, 20, 40, 60)
     */
    AudioService(AudioCodec* codec, int sample_rate, int channels, int frame_ms);

    /**
     * @brief Destroy audio service and cleanup resources
     */
    ~AudioService();

    /**
     * @brief Initialize audio service and FreeRTOS tasks
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Stop audio service and cleanup tasks
     */
    void end();

    /**
     * @brief Start audio capture and encoding
     *
     * @return true on success, false on failure
     */
    bool startCapture();

    /**
     * @brief Stop audio capture
     */
    void stopCapture();

    /**
     * @brief Start audio playback and decoding
     *
     * @return true on success, false on failure
     */
    bool startPlayback();

    /**
     * @brief Stop audio playback
     */
    void stopPlayback();

    /**
     * @brief Set callback for captured encoded audio packets
     *
     * Callback is invoked from capture task when Opus packets are ready.
     *
     * @param callback Function to call with encoded packets
     * @param user_data Optional user data pointer passed to callback
     */
    void setCaptureCallback(AudioPacketCallback callback, void* user_data = nullptr);

    /**
     * @brief Set callback for raw PCM audio samples from capture (for visualization)
     *
     * Callback is invoked from capture task with raw PCM samples before encoding.
     * Use this for audio visualization or level meters during recording.
     *
     * @param callback Function to call with PCM samples
     * @param user_data Optional user data pointer passed to callback
     */
    typedef void (*PcmSampleCallback)(const int16_t* samples, int count, void* user_data);
    void setPcmSampleCallback(PcmSampleCallback callback, void* user_data = nullptr);

    /**
     * @brief Set callback for raw PCM audio samples from playback (for visualization)
     *
     * Callback is invoked from output task with PCM samples being played to speaker.
     * Use this for audio visualization or level meters during TTS playback.
     *
     * @param callback Function to call with PCM samples
     * @param user_data Optional user data pointer passed to callback
     */
    void setPlaybackPcmCallback(PcmSampleCallback callback, void* user_data = nullptr);
    void setOpusTransmitEnabled(bool enabled);

    /**
     * @brief Set callback for send queue full events during capture
     *
     * Callback is invoked from capture task when the send queue is full.
     * Use this to trigger backoff or network error handling.
     *
     * @param callback Function to call on queue full events
     * @param user_data Optional user data pointer passed to callback
     */
    typedef void (*SendQueueFullCallback)(void* user_data);
    void setSendQueueFullCallback(SendQueueFullCallback callback, void* user_data = nullptr);

    /**
     * @brief Queue encoded audio packet for playback
     *
     * Packets are decoded and played back via FreeRTOS tasks.
     *
     * @param data Pointer to encoded Opus packet data
     * @param len Length of packet in bytes
     * @param timestamp Optional AEC timestamp in milliseconds
     * @return true if packet queued successfully, false if queue full
     */
    bool queuePlaybackPacket(const uint8_t* data, int len, uint32_t timestamp = 0);

    /**
     * @brief Pop encoded packet from send queue
     *
     * Used by network layer to retrieve captured packets for transmission.
     *
     * @param data Buffer to store packet data (must be ≥ AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE)
     * @param len Output: packet length in bytes
     * @param timestamp Output: optional AEC timestamp in milliseconds
     * @return true if packet retrieved, false if queue empty
     */
    bool popPacketFromSendQueue(uint8_t* data, int* len, uint32_t* timestamp = nullptr);

    /**
     * @brief Set Opus encoder bitrate
     *
     * @param bitrate Target bitrate in bits/second (2400 to 512000)
     */
    void setBitrate(int bitrate);

    /**
     * @brief Set Opus encoder computational complexity
     *
     * @param complexity Complexity level (0-10, higher = better quality, slower)
     */
    void setComplexity(int complexity);

    /**
     * @brief Check if audio capture is active
     *
     * @return true if capturing, false otherwise
     */
    bool isCaptureActive() const { return _capture_active; }
    void setCapturePaused(bool paused);

    /**
     * @brief Check if audio playback is active
     *
     * @return true if playing back, false otherwise
     */
    bool isPlaybackActive() const { return _playback_active; }

    /**
     * @brief Get configured sample rate
     *
     * @return Sample rate in Hz
     */
    int getSampleRate() const { return _sample_rate; }

    /**
     * @brief Get frame size in samples per channel
     *
     * @return Samples per frame
     */
    int getFrameSize() const { return _frame_size; }


private:
    enum class BackpressureLevel : uint8_t {
        NONE = 0,
        SOFT = 1,
        HARD = 2
    };

    // FreeRTOS task entry points

    /**
     * @brief Audio capture task entry point (priority 8)
     *
     * @param parameter Pointer to AudioService instance
     */
    static void captureTask(void* parameter);

    /**
     * @brief Audio output task entry point (priority 4)
     *
     * @param parameter Pointer to AudioService instance
     */
    static void audioOutputTask(void* parameter);

    /**
     * @brief Opus decode task entry point (priority 2)
     *
     * @param parameter Pointer to AudioService instance
     */
    static void opusDecodeTask(void* parameter);

    // Task implementations

    /**
     * @brief Capture task implementation (reads mic, encodes to Opus)
     */
    void captureTaskImpl();

    /**
     * @brief Audio output task implementation (writes PCM to I2S)
     */
    void audioOutputTaskImpl();

    /**
     * @brief Opus decode task implementation (decodes Opus to PCM)
     */
    void opusDecodeTaskImpl();

    // Audio components
    AudioCodec* _codec;
    OpusEncoder* _encoder;
    OpusDecoder* _decoder;
    // Configuration
    int _sample_rate;
    int _channels;
    int _frame_ms;
    int _frame_size;  // Samples per frame

    // State
    bool _capture_active;
    bool _capture_paused;
    bool _playback_active;
    bool _initialized;
    bool _opus_transmit_enabled;
    BackpressureLevel _backpressure_level;

    // Callbacks
    AudioPacketCallback _capture_callback;
    void* _capture_callback_user_data;
    PcmSampleCallback _pcm_sample_callback;
    void* _pcm_sample_callback_user_data;
    PcmSampleCallback _playback_pcm_callback;
    void* _playback_pcm_callback_user_data;
    SendQueueFullCallback _send_queue_full_callback;
    void* _send_queue_full_callback_user_data;
    uint32_t _send_queue_full_last_ms;

    // FreeRTOS queues
    QueueHandle_t _opus_decode_queue;  // Opus packets from WebSocket → Decoder
    QueueHandle_t _pcm_output_queue;   // PCM samples from Decoder → I2S output
    QueueHandle_t _send_queue;         // Encoded packets to send

    // Queue sizes (optimized for 1200ms buffering, saves 40 KB SRAM)
    static const int OPUS_DECODE_QUEUE_SIZE = 20;  // 20 packets × 60ms = 1200ms buffering
    static const int PCM_OUTPUT_QUEUE_SIZE = 2;    // Ring buffer provides main buffering
    static const int SEND_QUEUE_SIZE = 20;         // 20 packets × 60ms = 1200ms buffering

    // Queue item structures

    /**
     * Opus packet queue item
     */
    struct OpusPacket {
        uint8_t data[AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE];
        int len;
        uint32_t timestamp;  // For server-side AEC
    };

    /**
     * PCM packet queue item
     */
    struct PcmPacket {
        int16_t* data;       // Pointer to PCM buffer in ring buffer
        int samples;         // Number of samples
        uint32_t timestamp;  // For server-side AEC
    };

    // Thread safety
    SemaphoreHandle_t _state_mutex;

    // Timestamp queue for server-side AEC
    QueueHandle_t _timestamp_queue;
    static const int TIMESTAMP_QUEUE_SIZE = 3;

    // Buffers
    int16_t* _pcm_buffer;
    uint8_t* _opus_buffer;

    // Resamplers
    OpusResampler _input_resampler;
    OpusResampler _output_resampler;

    // Resampling buffer for capture
    int16_t* _resample_buffer;
    int _resample_buffer_size;

    void notifySendQueueFull();

    // Ring buffer (eliminates 781 KB/s memory churn)
    PcmRingBuffer _ring_buffer;

};
