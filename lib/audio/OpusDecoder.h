/**
 * @brief library.
 * OpusDecoder.h
 *
 * Declarations for OpusDecoder.
 */

#pragma once

// System includes
#include <Arduino.h>

// Opus library (C library)
extern "C" {
    #include <opus.h>
}

/**
 * OpusDecoder - Opus audio decoder
 *
 * Features:
 * - Opus packet decoding to PCM samples
 * - Forward Error Correction (FEC) support
 * - Configurable sample rate and channels
 *
 * Common use cases:
 * - Decode 16kHz mono Opus packets from network
 * - Decode with FEC for packet loss recovery
 *
 * Hardware: Used with ESP32-S3 I2S for audio output
 */
class OpusDecoder {
public:
    /**
     * @brief Construct Opus decoder instance
     *
     * @param sample_rate Sample rate in Hz (8000, 12000, 16000, 24000, 48000)
     * @param channels Number of channels (1=mono, 2=stereo)
     */
    OpusDecoder(int sample_rate, int channels);

    /**
     * @brief Destroy decoder and free resources
     */
    ~OpusDecoder();

    /**
     * @brief Initialize Opus decoder
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Stop and cleanup decoder resources
     */
    void end();

    /**
     * @brief Decode Opus packet to PCM samples
     *
     * @param opus_data Pointer to encoded Opus packet data
     * @param opus_len Length of Opus packet in bytes
     * @param pcm Output buffer for decoded PCM samples
     * @param max_samples Maximum samples per channel that pcm buffer can hold
     * @return Number of decoded samples per channel, or negative on error
     */
    int decode(const uint8_t* opus_data, int opus_len, int16_t* pcm, int max_samples);

    /**
     * @brief Decode with Forward Error Correction (FEC)
     *
     * Attempts to reconstruct lost packet using FEC data from next packet.
     *
     * @param pcm Output buffer for decoded PCM samples
     * @param max_samples Maximum samples per channel that pcm buffer can hold
     * @return Number of decoded samples per channel, or negative on error
     */
    int decodeFec(int16_t* pcm, int max_samples);

    /**
     * @brief Reset decoder state to initial conditions
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
     * @brief Check if decoder is initialized
     *
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return _decoder != nullptr; }

private:
    // Opus library decoder state
    ::OpusDecoder* _decoder;

    // Configuration
    int _sample_rate;
    int _channels;
};

