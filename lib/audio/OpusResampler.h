/**
 * @brief library.
 * OpusResampler.h
 *
 * Declarations for OpusResampler.
 */

#pragma once

// Standard includes
#include <cstdint>

// Opus library (C library)
extern "C" {
    #include "opus.h"
    #include "SigProc_FIX.h"  // Contains silk_resampler functions from esp32_opus library
}

/**
 * OpusResampler - Audio sample rate converter
 *
 * Features:
 * - High-quality audio resampling
 * - Uses SILK resampler from Opus library
 * - Supports arbitrary sample rate conversions
 *
 * Common use cases:
 * - 24kHz (hardware) → 16kHz (encoder)
 * - 16kHz (decoder) → 24kHz (hardware)
 */
class OpusResampler {
public:
    /**
     * @brief Construct Opus resampler instance
     */
    OpusResampler();

    /**
     * @brief Destroy resampler and free resources
     */
    ~OpusResampler();

    /**
     * @brief Configure resampler for specific sample rates
     *
     * @param input_sample_rate Input sample rate in Hz
     * @param output_sample_rate Output sample rate in Hz
     */
    void Configure(int input_sample_rate, int output_sample_rate);

    /**
     * @brief Process audio samples (resample)
     *
     * @param input Pointer to input samples
     * @param input_samples Number of input samples
     * @param output Pointer to output buffer (must be large enough)
     */
    void Process(const int16_t *input, int input_samples, int16_t *output);

    /**
     * @brief Calculate number of output samples for given input count
     *
     * @param input_samples Number of input samples
     * @return Number of output samples that will be produced
     */
    int GetOutputSamples(int input_samples) const;

    /**
     * @brief Get configured input sample rate
     *
     * @return Input sample rate in Hz
     */
    int input_sample_rate() const { return input_sample_rate_; }

    /**
     * @brief Get configured output sample rate
     *
     * @return Output sample rate in Hz
     */
    int output_sample_rate() const { return output_sample_rate_; }

private:
    // SILK resampler state (from Opus library)
    silk_resampler_state_struct resampler_state_;

    // Configuration
    int input_sample_rate_;
    int output_sample_rate_;
};
