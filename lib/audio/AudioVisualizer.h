/**
 * AudioVisualizer.h
 *
 * Simple audio visualization using RMS (Root Mean Square) calculation.
 * Provides audio level meters and peak detection.
 * Optimized for low RAM usage (Synchronous processing, no queues).
 *
 * Author: Byte90 Team
 * Board: XIAO ESP32-S3 (Seeed Studio XIAO ESP32S3 - BYTE 90 board)
 */

#pragma once

#include <Arduino.h>
#include <cmath>
#include <atomic>

/**
 * @brief AudioVisualizer.
 * AudioVisualizer - Real-time Audio Level Visualization
 *
 * Calculates RMS (Root Mean Square) values for audio visualization.
 * Provides smoothed levels, peak detection, and level bars.
 *
 * OPTIMIZED: Synchronous processing (no task, no queues).
 * RAM Usage: Negligible (<100 bytes).
 *
 * Usage:
 *   AudioVisualizer viz;
 *   viz.begin();
 *   viz.processAudio(audio_samples, num_samples); // Call this directly
 *   float level = viz.getRmsLevel();      // 0.0 to 1.0
 */
class AudioVisualizer {
public:
    AudioVisualizer();
    ~AudioVisualizer();

    /**
     * @brief Initialize visualizer
     */
    bool begin();

    /**
     * @brief Stop visualizer
     */
    void end();

    /**
     * @brief Process audio samples immediately to update levels.
     * This replaces queueAudioSamples. It is fast enough to call on the audio thread.
     * 
     * @param samples Audio samples (16-bit signed PCM)
     * @param count Number of samples to process
     */
    void processAudio(const int16_t* samples, int count);

    // Keep backwards compatibility alias if needed, or just replace usage
    bool queueAudioSamples(const int16_t* samples, int count) {
        processAudio(samples, count);
        return true;
    }

    float getRmsLevel() const { return _rms_level.load(); }
    float getSmoothedLevel() const { return _smoothed_level.load(); }
    float getPeakLevel() const { return _peak_level.load(); }
    uint32_t getLastUpdateMs() const { return _last_update_ms.load(); }

    int getLevelBars(int max_bars) const;
    int getLevelPercentage() const;
    bool isClipping() const { return _is_clipping.load(); }
    float getSpeechLevel() const { return _smoothed_level.load(); }

    void reset();
    void setSmoothingFactor(float factor);
    void setPeakHoldTime(unsigned long ms) { _peak_hold_time = ms; }
    bool isRunning() const { return _running; }

private:
    float calculateRms(const int16_t* samples, int count);

    bool _running;

    // Use atomic for thread safety since processAudio() runs on audio task
    // and getters run on UI task.
    std::atomic<float> _rms_level;
    std::atomic<float> _smoothed_level;
    std::atomic<float> _peak_level;
    std::atomic<bool> _is_clipping;
    std::atomic<uint32_t> _last_update_ms;

    float _smoothing_factor;
    unsigned long _peak_time;
    unsigned long _peak_hold_time;

    static constexpr float CLIPPING_THRESHOLD = 0.95f;
};
