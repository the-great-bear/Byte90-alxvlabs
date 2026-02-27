/**
 * AudioService.h
 *
 * Declarations for AudioService.
 */

#pragma once

// Project includes
#include "AudioCodec.h"
#include "DeviceConfig.h"
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
 * - Real-time audio capture with Opus encoding
 * - Server-side AEC timestamp support (capture only)
 *
 * Architecture:
 * - Capture task (priority 8): Reads from microphone, encodes to Opus
 * - No Opus playback tasks in the OpenAI pipeline
 *
 * Memory optimization:
 * - Reduced queue sizes save SRAM
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

    /**
     * @brief Configure OpenAI resampler for capture (16kHz → 24kHz)
     *
     * @param input_rate Input sample rate in Hz (e.g., 16000)
     * @param output_rate Output sample rate in Hz (e.g., 24000)
     * @return true on success, false on failure
     */
    bool configureOpenAIResampler(int input_rate, int output_rate);

    /**
     * @brief Start PCM playback from OpenAI ring buffer
     *
     * Creates a FreeRTOS task that continuously reads from the provided
     * ring buffer source and writes PCM audio to the codec.
     *
     * @param source Pointer to ring buffer interface (OpenAIWebsocket)
     * @param prime_ms Buffer priming duration in milliseconds (default 60ms)
     * @return true on success, false on failure
     */
    bool startPcmPlayback(void* source, int prime_ms = 60);

    /**
     * @brief Stop PCM playback task
     */
    void stopPcmPlayback();

    /**
     * @brief Check if PCM playback is active
     *
     * @return true if playing back, false otherwise
     */
    bool isPcmPlaybackActive() const { return _pcm_playback_active; }

    /**
     * @brief Process captured PCM samples through OpenAI resampler
     *
     * Called from capture callback, resamples input to output buffer.
     *
     * @param samples Input PCM samples (16kHz)
     * @param count Number of input samples
     * @param output Output buffer for resampled data (24kHz)
     * @return Number of output samples written
     */
    int processOpenAICapture(const int16_t* samples, int count, int16_t* output);

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

    // Task implementations

    /**
     * @brief Capture task implementation (reads mic, encodes to Opus)
     */
    void captureTaskImpl();

    // Audio components
    AudioCodec* _codec;
    OpusEncoder* _encoder;
    // Configuration
    int _sample_rate;
    int _channels;
    int _frame_ms;
    int _frame_size;  // Samples per frame

    // State
    bool _capture_active;
    bool _capture_paused;
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
    QueueHandle_t _send_queue;         // Encoded packets to send

    // Queue sizes (optimized for 1200ms buffering, saves 40 KB SRAM)
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

    // Thread safety
    SemaphoreHandle_t _state_mutex;

    // Buffers
    int16_t* _pcm_buffer;
    uint8_t* _opus_buffer;

    // Resamplers
    OpusResampler _input_resampler;

    // Resampling buffer for capture
    int16_t* _resample_buffer;
    int _resample_buffer_size;

    void notifySendQueueFull();

    // OpenAI PCM playback task entry point
    static void pcmPlaybackTask(void* parameter);
    void pcmPlaybackTaskImpl();

    // OpenAI resampler for capture (16kHz → 24kHz)
    OpusResampler _openai_resampler;
    int16_t* _openai_resample_buffer;
    int _openai_resample_buffer_size;
    bool _openai_resampler_ready;

    // PCM playback state
    bool _pcm_playback_active;
    void* _pcm_playback_source;  // Pointer to OpenAIWebsocket
    int _pcm_prime_ms;
};
