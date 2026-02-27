/**
 * AudioVisualizer.cpp
 *
 * Implementation for AudioVisualizer.
 */

#include "AudioVisualizer.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "AudioVisualizer";

AudioVisualizer::AudioVisualizer()
    : _running(false),
      _rms_level(0.0f),
      _smoothed_level(0.0f),
      _peak_level(0.0f),
      _is_clipping(false),
      _last_update_ms(0),
      _smoothing_factor(0.7f),
      _peak_time(0),
      _peak_hold_time(1000)
{
}

AudioVisualizer::~AudioVisualizer()
{
    end();
}

bool AudioVisualizer::begin()
{
    if (_running) return true;
    _running = true;
    reset();
    ESP_LOGI(TAG, "Initialized (Optimized: Direct Mode)");
    return true;
}

void AudioVisualizer::end()
{
    _running = false;
}

void AudioVisualizer::processAudio(const int16_t* samples, int count)
{
    if (!_running || !samples || count <= 0) return;

    _last_update_ms.store(millis());

    // 1. Calculate RMS immediately
    float current_rms = calculateRms(samples, count);
    
    // 2. Update Atomic Current Level
    _rms_level.store(current_rms);

    // 3. Apply Smoothing
    // We do a read-modify-write here. It's technically racy if multiple threads 
    // call processAudio, but that shouldn't happen (single audio task).
    // UI thread only reads.
    float prev_smoothed = _smoothed_level.load();
    float new_smoothed;

    if (current_rms > prev_smoothed) {
        new_smoothed = current_rms; // Fast rise
    } else {
        new_smoothed = _smoothing_factor * prev_smoothed + (1.0f - _smoothing_factor) * current_rms; // Slow decay
    }
    _smoothed_level.store(new_smoothed);

    // 4. Update Peak
    unsigned long now = millis();
    float current_peak = _peak_level.load();

    if (current_rms > current_peak) {
        _peak_level.store(current_rms);
        _peak_time = now;
    } else if (now - _peak_time > _peak_hold_time) {
        // Simple decay for peak
        _peak_level.store(current_rms); 
    }

    // 5. Clipping
    _is_clipping.store(current_rms >= CLIPPING_THRESHOLD);
}

float AudioVisualizer::calculateRms(const int16_t* samples, int count)
{
    if (!samples || count <= 0) return 0.0f;

    float sum_squares = 0.0f;
    // Optimization: Don't process every single sample if buffer is huge.
    // Processing every 2nd or 4th sample is statistically enough for visualizer.
    // But for 160-320 samples (10-20ms), full processing is fine (~20us).
    for (int i = 0; i < count; i++) {
        float normalized = samples[i] / 32768.0f;
        sum_squares += normalized * normalized;
    }
    float rms = sqrtf(sum_squares / count);
    
    if (rms > 1.0f) rms = 1.0f;
    return rms;
}

int AudioVisualizer::getLevelBars(int max_bars) const
{
    if (max_bars <= 0) return 0;
    int bars = (int)(_smoothed_level.load() * max_bars);
    if (bars < 0) bars = 0;
    if (bars > max_bars) bars = max_bars;
    return bars;
}

int AudioVisualizer::getLevelPercentage() const
{
    int percentage = (int)(_smoothed_level.load() * 100.0f);
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    return percentage;
}

void AudioVisualizer::reset()
{
    _rms_level.store(0.0f);
    _smoothed_level.store(0.0f);
    _peak_level.store(0.0f);
    _is_clipping.store(false);
    _last_update_ms.store(0);
    _peak_time = 0;
}

void AudioVisualizer::setSmoothingFactor(float factor)
{
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    _smoothing_factor = factor;
}
