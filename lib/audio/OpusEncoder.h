/**
 * @brief library.
 * OpusEncoder.h
 *
 * Declarations for OpusEncoder.
 */

#pragma once

// System includes
#include <Arduino.h>

// Opus library (C library)
extern "C" {
    #include <opus.h>
}

/**
 * OpusEncoder - Opus audio encoder
 *
 * Features:
 * - PCM to Opus packet encoding
 * - Configurable bitrate, complexity, and signal type
 * - Discontinuous Transmission (DTX) support
 * - Multiple frame sizes (2.5ms to 60ms)
 *
 * Common use cases:
 * - Encode 16kHz mono PCM from microphone to Opus
 * - Voice encoding with OPUS_SIGNAL_VOICE
 * - Low-latency encoding with 20ms frames
 *
 * Hardware: Used with ESP32-S3 I2S for audio input
 */
class OpusEncoder {
public:
    /**
     * @brief Construct Opus encoder instance
     *
     * @param sample_rate Sample rate in Hz (8000, 12000, 16000, 24000, 48000)
     * @param channels Number of channels (1=mono, 2=stereo)
     * @param frame_ms Frame duration in milliseconds (2.5, 5, 10, 20, 40, 60)
     */
    OpusEncoder(int sample_rate, int channels, int frame_ms);

    /**
     * @brief Destroy encoder and free resources
     */
    ~OpusEncoder();

    /**
     * @brief Initialize Opus encoder
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Stop and cleanup encoder resources
     */
    void end();

    /**
     * @brief Encode PCM samples to Opus packet
     *
     * @param pcm Input PCM samples (must contain exactly frame_size samples)
     * @param samples Number of samples per channel (must equal frame_size)
     * @param output Output buffer for encoded Opus packet
     * @param max_output_size Maximum size of output buffer in bytes
     * @return Encoded packet size in bytes, or negative on error
     */
    int encode(const int16_t* pcm, int samples, uint8_t* output, int max_output_size);

    /**
     * @brief Set encoder bitrate
     *
     * @param bitrate Target bitrate in bits/second (2400 to 512000)
     * @return true on success, false on failure
     */
    bool setBitrate(int bitrate);

    /**
     * @brief Set encoder computational complexity
     *
     * @param complexity Complexity level (0-10, higher = better quality, slower)
     * @return true on success, false on failure
     */
    bool setComplexity(int complexity);

    /**
     * @brief Set signal type hint for encoder optimization
     *
     * @param signal_type OPUS_AUTO, OPUS_SIGNAL_VOICE, or OPUS_SIGNAL_MUSIC
     * @return true on success, false on failure
     */
    bool setSignal(int signal_type);

    /**
     * @brief Enable or disable Discontinuous Transmission
     *
     * DTX reduces bitrate during silence by not transmitting packets.
     *
     * @param enable true to enable DTX, false to disable
     * @return true on success, false on failure
     */
    bool setDtx(bool enable);

    /**
     * @brief Reset encoder state to initial conditions
     */
    void reset();

    /**
     * @brief Get configured sample rate
     *
     * @return Sample rate in Hz
     */
    int getSampleRate() const { return _sample_rate; }

    /**
     * @brief Get configured number of channels
     *
     * @return Number of channels (1=mono, 2=stereo)
     */
    int getChannels() const { return _channels; }

    /**
     * @brief Get frame size in samples per channel
     *
     * @return Samples per frame
     */
    int getFrameSize() const { return _frame_size; }

    /**
     * @brief Check if encoder is initialized
     *
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return _encoder != nullptr; }

private:
    // Opus library encoder state
    ::OpusEncoder* _encoder;

    // Configuration
    int _sample_rate;
    int _channels;
    int _frame_size;  // Samples per frame
    int _frame_ms;    // Frame duration in milliseconds
};

