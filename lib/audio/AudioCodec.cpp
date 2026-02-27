/**
 * AudioCodec.cpp
 *
 * Implementation for AudioCodec.
 */

#include "AudioCodec.h"
#include "NvsStorage.h"
#include <esp_log.h>

static const char* TAG = "AudioCodec";

// I2S microphone constructor (ICS-43434)
AudioCodec::AudioCodec(int input_sample_rate, int output_sample_rate,
                       int8_t spk_bclk, int8_t spk_lrck, int8_t spk_dout,
                       int8_t mic_i2s_data, NVSStorage* storage)
    : _input_sample_rate(input_sample_rate),
      _output_sample_rate(output_sample_rate),
      _output_volume(DEFAULT_OUTPUT_VOLUME),
      _input_gain(DEFAULT_INPUT_GAIN),
      _input_enabled(false),
      _output_enabled(false),
      _muted(false),
      _initialized(false),
      _mic_initialized(false),
      _spk_initialized(false),
      _i2s_started(false),
      _mic_i2s_data(mic_i2s_data),
      _spk_bclk(spk_bclk), _spk_lrck(spk_lrck), _spk_dout(spk_dout),
      _storage(storage),
      _power_timer(nullptr),
      _last_input_time(0),
      _last_output_time(0),
      _i2s_read_buffer(nullptr),
      _i2s_write_buffer(nullptr),
      _max_buffer_samples(0) {
}

AudioCodec::~AudioCodec() {
    ESP_LOGI(TAG, "AudioCodec destructor: cleaning up resources");

    // Stop I2S if running
    if (_i2s_started) {
        i2s_stop(I2S_NUM_FULLDUPLEX);
        _i2s_started = false;
    }

    // Uninstall I2S driver
    if (_mic_initialized || _spk_initialized) {
        i2s_driver_uninstall(I2S_NUM_FULLDUPLEX);
        _mic_initialized = false;
        _spk_initialized = false;
    }

    // Free pre-allocated PSRAM buffers
    if (_i2s_read_buffer) {
        ESP_LOGI(TAG, "Freeing I2S read buffer @ %p", _i2s_read_buffer);
        free(_i2s_read_buffer);
        _i2s_read_buffer = nullptr;
    }

    if (_i2s_write_buffer) {
        ESP_LOGI(TAG, "Freeing I2S write buffer @ %p", _i2s_write_buffer);
        free(_i2s_write_buffer);
        _i2s_write_buffer = nullptr;
    }

    // Delete power timer
    if (_power_timer) {
        esp_timer_stop(_power_timer);
        esp_timer_delete(_power_timer);
        _power_timer = nullptr;
    }

    ESP_LOGI(TAG, "AudioCodec cleanup complete");
}

bool AudioCodec::begin() {
    ESP_LOGI(TAG, "Initializing Audio Codec (Full-Duplex I2S)");

    // Initialize microphone and speaker (full-duplex)
    if (!initMicrophoneI2S()) {
        ESP_LOGE(TAG, "❌ Failed to initialize full-duplex I2S");
        return false;
    }

    // Pre-allocate I2S conversion buffers in PSRAM (zero-churn optimization)
    // Max buffer size: 2048 samples for generous headroom
    // - Read: typically 256 samples (I2S DMA buffer)
    // - Write: typically 1440 samples (60ms @ 24kHz)
    _max_buffer_samples = 2048;
    size_t buffer_size = _max_buffer_samples * 2 * sizeof(int32_t);  // stereo pairs

    ESP_LOGI(TAG, "Allocating I2S conversion buffers in PSRAM...");
    ESP_LOGI(TAG, "  Buffer size: %d KB each (%d samples × 2 channels × 4 bytes)",
             buffer_size / 1024, _max_buffer_samples);

    // Log memory before allocation
    size_t psram_free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    _i2s_read_buffer = (int32_t*)heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    _i2s_write_buffer = (int32_t*)heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!_i2s_read_buffer || !_i2s_write_buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate I2S conversion buffers in PSRAM");
        ESP_LOGE(TAG, "   PSRAM free: %d KB, Internal free: %d KB",
                 psram_free_before / 1024, internal_free_before / 1024);
        if (_i2s_read_buffer) {
            free(_i2s_read_buffer);
            _i2s_read_buffer = nullptr;
        }
        if (_i2s_write_buffer) {
            free(_i2s_write_buffer);
            _i2s_write_buffer = nullptr;
        }
        return false;
    }

    // Log memory after allocation
    size_t psram_free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "✅ I2S conversion buffers allocated in PSRAM:");
    ESP_LOGI(TAG, "   Read buffer:  %p (%d KB)", _i2s_read_buffer, buffer_size / 1024);
    ESP_LOGI(TAG, "   Write buffer: %p (%d KB)", _i2s_write_buffer, buffer_size / 1024);
    ESP_LOGI(TAG, "   Total allocated: %d KB", (buffer_size * 2) / 1024);
    ESP_LOGI(TAG, "   PSRAM:    %d KB → %d KB (used: %d KB)",
             psram_free_before / 1024, psram_free_after / 1024,
             (psram_free_before - psram_free_after) / 1024);
    ESP_LOGI(TAG, "   Internal: %d KB → %d KB (no change)",
             internal_free_before / 1024, internal_free_after / 1024);
    ESP_LOGI(TAG, "   Zero-churn optimization: ACTIVE ✓");

    // Create power management timer
    esp_timer_create_args_t timer_args = {
        .callback = &AudioCodec::powerTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true
    };

    esp_err_t ret = esp_timer_create(&timer_args, &_power_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create power timer: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Audio codec initialization complete");

    return true;
}

bool AudioCodec::initMicrophoneI2S() {
    ESP_LOGD(TAG, "Configuring Full-Duplex I2S (Microphone + Speaker) on I2S0");
    ESP_LOGD(TAG, "  Mode: MASTER | RX | TX (full-duplex)");
    ESP_LOGD(TAG, "  Shared BCLK (GPIO%d) and WS (GPIO%d) for both mic and speaker", 
             _spk_bclk, _spk_lrck);
    ESP_LOGD(TAG, "  Mic DATA: GPIO%d, Speaker DATA: GPIO%d", _mic_i2s_data, _spk_dout);
    ESP_LOGD(TAG, "  Sample rate: %d Hz (shared for both)", _output_sample_rate);
    
    // Use speaker's sample rate (mic will be resampled in software)
    uint32_t sample_rate = (uint32_t)_output_sample_rate;
    
    // Full-duplex I2S configuration
    // ICS-43434: Must use stereo format for LEFT channel to work in full-duplex
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo required
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    esp_err_t ret = i2s_driver_install(I2S_NUM_FULLDUPLEX, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to install full-duplex I2S driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Configure pins: shared clocks, separate data pins
    i2s_pin_config_t pins = {
        .bck_io_num = _spk_bclk,      // Shared BCLK
        .ws_io_num = _spk_lrck,        // Shared WS/LRCLK
        .data_out_num = _spk_dout,     // Speaker data (TX)
        .data_in_num = _mic_i2s_data   // Microphone data (RX)
    };
    
    ret = i2s_set_pin(I2S_NUM_FULLDUPLEX, &pins);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to set I2S pins: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set STEREO clock to match stereo channel format
    ret = i2s_set_clk(I2S_NUM_FULLDUPLEX, sample_rate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "🟡 Failed to set I2S clock: %s (may not be critical)", esp_err_to_name(ret));
    }
    
    // Zero DMA buffers and add delay for stabilization (matching working config)
    i2s_zero_dma_buffer(I2S_NUM_FULLDUPLEX);
    ESP_LOGD(TAG, "Waiting for I2S to stabilize...");
    delay(100);  // Initial delay
    delay(500);  // Stabilization delay (total 600ms like working config)
    
    _mic_initialized = true;
    _spk_initialized = true;  // Speaker is part of full-duplex setup
    
    ESP_LOGI(TAG, "✅ Full-duplex I2S configured successfully");
    ESP_LOGD(TAG, "  Hardware sample rate: %d Hz", sample_rate);
    ESP_LOGD(TAG, "  Mic target rate: %d Hz (will be resampled)", _input_sample_rate);
    ESP_LOGD(TAG, "  Bits per sample: 32-bit (mic: 24-bit audio in upper 24 bits)");
    ESP_LOGD(TAG, "  Channel format: Mono LEFT (ICS-43434 hardwired to LEFT channel)");
    ESP_LOGD(TAG, "  Microphone: ICS-43434 (I2S MEMS, LEFT channel)");
    
    return true;
}

void AudioCodec::start() {
    if (!_initialized) {
        ESP_LOGW(TAG, "🟡 Audio codec not initialized");
        return;
    }

    // Load volume and mute state from NVS
    if (_storage) {
        audio_settings_t settings{};
        if (_storage->loadAudioSettings(&settings)) {
            _output_volume = constrain(settings.volume, 0, 100);
            _muted = !settings.enabled;
            ESP_LOGD(TAG, "Loaded audio settings: vol=%d muted=%d",
                     _output_volume, _muted ? 1 : 0);
        } else {
            _output_volume = DEFAULT_OUTPUT_VOLUME;
            _muted = false;
            ESP_LOGW(TAG, "Failed to load audio settings, using defaults");
        }
    }

    setInputGain(DEFAULT_INPUT_GAIN);

    ESP_LOGI(TAG, "Starting audio channels");
    ESP_LOGD(TAG, "  Full-duplex I2S: Single port handles both mic (RX) and speaker (TX)");
    ESP_LOGD(TAG, "  Both channels can be active simultaneously");
    
    // Start full-duplex I2S port
    if (_mic_initialized && _spk_initialized) {
        esp_err_t ret = i2s_start(I2S_NUM_FULLDUPLEX);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Failed to start full-duplex I2S: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGD(TAG, "Full-duplex I2S started");
            _i2s_started = true;
            _input_enabled = true;
            _output_enabled = !_muted;
            
            // Dummy read to kickstart RX
            int32_t dummy_buffer[16];
            size_t bytes_read = 0;
            i2s_read(I2S_NUM_FULLDUPLEX, dummy_buffer, sizeof(dummy_buffer), &bytes_read, 0);
        }
    }

    // Initialize timestamps
    _last_input_time = millis();
    _last_output_time = millis();

    // Start power management timer (check every 1 second)
    if (_power_timer) {
        esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
    }

    ESP_LOGI(TAG, "Audio channels started");
}

void AudioCodec::stop() {
    if (!_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Stopping audio channels");

    // Stop power management timer
    if (_power_timer) {
        esp_timer_stop(_power_timer);
    }

    // Full-duplex: single I2S port
    if (_mic_initialized && _spk_initialized) {
        i2s_stop(I2S_NUM_FULLDUPLEX);
        _input_enabled = false;
        _output_enabled = false;
    }
}

void AudioCodec::setOutputVolume(int volume) {
    _output_volume = constrain(volume, 0, 100);
    ESP_LOGI(TAG, "Output volume set to %d%%", _output_volume);

    // Save to NVS
    if (_storage && _storage->beginAudio(false)) {  // Read-write
        Preferences& prefs = _storage->getAudioPrefs();
        prefs.putInt("volume", _output_volume);
        _storage->endAudio();
        ESP_LOGD(TAG, "Volume saved to NVS");
    } else {
        ESP_LOGW(TAG, "🟡 Failed to save volume to NVS");
    }
}

void AudioCodec::setInputGain(float gain) {
    _input_gain = constrain(gain, 0.5f, 10.0f);
    ESP_LOGD(TAG, "Input gain set to %.2f", _input_gain);
}

void AudioCodec::enableInput(bool enable) {
    if (!_initialized || !_mic_initialized) {
        return;
    }

    // Full-duplex: input and output share the same port
    if (enable && !_input_enabled) {
        // Restart power timer
        if (_power_timer) {
            esp_timer_stop(_power_timer);
            esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        }
        
        // Check if I2S is actually started
        if (!_i2s_started) {
            ESP_LOGD(TAG, "🟡  FIX: I2S not started, starting now (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
            esp_err_t ret = i2s_start(I2S_NUM_FULLDUPLEX);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Failed to start full-duplex I2S: %s", esp_err_to_name(ret));
                return;
            }
            _i2s_started = true;
            _output_enabled = true;  // Output is also enabled (shared port)
        
            // Zero DMA buffer ONLY when just started
            i2s_zero_dma_buffer(I2S_NUM_FULLDUPLEX);
            delay(1000);  // Allow microphone to stabilize
        
            // Dummy read to kickstart RX
            int32_t dummy_buffer[16];
            size_t bytes_read = 0;
            i2s_read(I2S_NUM_FULLDUPLEX, dummy_buffer, sizeof(dummy_buffer), &bytes_read, pdMS_TO_TICKS(10));
        } else {
            ESP_LOGD(TAG, "🟡  FIX: I2S already running, not zeroing DMA (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
        }
        
        _input_enabled = true;
        _last_input_time = millis();
    } else if (!enable && _input_enabled) {
        // Note: We can't stop RX without stopping TX in full-duplex mode
        // So we just mark input as disabled but keep I2S running for output
        _input_enabled = false;
        ESP_LOGI(TAG, "Microphone disabled (I2S continues for speaker)");
    }
}

void AudioCodec::enableOutput(bool enable) {
    if (!_initialized || !_spk_initialized) {
        return;
    }

    // Full-duplex: input and output share the same port
    if (enable && !_output_enabled) {
        if (_muted) {
            ESP_LOGI(TAG, "Speaker enable ignored (muted)");
            return;
        }
        if (_power_timer) {
            esp_timer_stop(_power_timer);
            esp_timer_start_periodic(_power_timer, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        }
        
        // Check if I2S is actually started
        if (!_i2s_started) {
            ESP_LOGD(TAG, "🟡  FIX: I2S not started, starting now (input: %d, output: %d)",
                     _input_enabled, _output_enabled);
            esp_err_t ret = i2s_start(I2S_NUM_FULLDUPLEX);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Failed to start full-duplex I2S: %s", esp_err_to_name(ret));
                return;
            }
            _i2s_started = true;
        }
        
        _output_enabled = true;
        _input_enabled = true;  // Input is also enabled (shared port)
        _last_output_time = millis();
        ESP_LOGI(TAG, "Speaker enabled (full-duplex mode - mic also active)");
    } else if (!enable && _output_enabled) {
        // Note: We can't stop TX without stopping RX in full-duplex mode
        // So we just mark output as disabled but keep I2S running for input
        _output_enabled = false;
        ESP_LOGI(TAG, "Speaker disabled (I2S continues for microphone)");
    }
}

void AudioCodec::setMuted(bool muted) {
    _muted = muted;
    if (muted) {
        enableOutput(false);
    }
}

int AudioCodec::read(int16_t* buffer, int samples) {
    if (!_initialized || !_mic_initialized || !buffer) {
        return 0;
    }

    // Auto-enable input if it was disabled
    if (!_input_enabled) {
        ESP_LOGW(TAG, "🟡 Read called but input disabled! Auto-enabling... (output enabled: %d)", _output_enabled);
        enableInput(true);
    }

    // I2S mic: read 32-bit stereo samples, extract LEFT channel (ICS-43434)
    static int read_count = 0;
    read_count++;
    
    if (read_count == 1) {
        ESP_LOGD(TAG, "[Mic Debug] FIRST READ at t=%lu", millis());
        ESP_LOGI(TAG, "[Zero-Churn] Using pre-allocated PSRAM buffer @ %p", _i2s_read_buffer);
    }

    // Bounds check: ensure samples fit in pre-allocated buffer
    if (samples > _max_buffer_samples) {
        ESP_LOGW(TAG, "Read samples (%d) exceeds buffer size (%d), clamping",
                 samples, _max_buffer_samples);
        samples = _max_buffer_samples;
    }

    // Read stereo pairs (required for LEFT channel to work)
    size_t bytes_to_read = samples * 2 * sizeof(int32_t);
    size_t bytes_read = 0;

    // Use pre-allocated PSRAM buffer (zero-churn optimization)
    esp_err_t ret = i2s_read(I2S_NUM_FULLDUPLEX, _i2s_read_buffer, bytes_to_read,
                             &bytes_read, pdMS_TO_TICKS(100));

    if (ret != ESP_OK) {
        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "[Mic Debug] I2S read failed: %s", esp_err_to_name(ret));
        }
        return 0;  // No free() needed - buffer is persistent
    }

    if (bytes_read == 0) {
        if (read_count % 100 == 0) {
            ESP_LOGD(TAG, "[Mic Debug] No bytes read from I2S");
        }
        return 0;  // No free() needed - buffer is persistent
    }

    // Log every 100 reads
    if (read_count % 100 == 0) {
        ESP_LOGD(TAG, "[Mic Debug] t=%lu Read %d bytes (%d stereo pairs) from I2S",
                 millis(), bytes_read, bytes_read / (2 * sizeof(int32_t)));
    }

    int stereo_pairs = bytes_read / (2 * sizeof(int32_t));

    if (read_count % 100 == 0) {
        ESP_LOGD(TAG, "[Mic Raw] t=%lu First 2 pairs: L=0x%08X R=0x%08X, L=0x%08X R=0x%08X",
                 millis(), _i2s_read_buffer[0], _i2s_read_buffer[1],
                 _i2s_read_buffer[2], _i2s_read_buffer[3]);
    }

    // Convert 32-bit to 16-bit, sum both channels to handle random channel swapping
    int16_t min_sample = INT16_MAX;
    int16_t max_sample = INT16_MIN;
    int32_t sum_abs = 0;
    bool has_non_zero = false;

    for (int i = 0; i < stereo_pairs && i < samples; i++) {
        // Read both channels and sum them to handle random channel swapping on boot
        // ICS-43434 is mono but due to I2S timing it might appear on Left or Right
        int32_t raw_l = _i2s_read_buffer[i * 2];
        int32_t raw_r = _i2s_read_buffer[i * 2 + 1];

            // ICS-43434: 24-bit data in UPPER 24 bits (bits 8-31) - same as SPH0645LM4H-B
            int32_t sample_l_24bit = (raw_l >> 8) & 0x00FFFFFF;
            if (sample_l_24bit & 0x00800000) {
                sample_l_24bit |= 0xFF000000;
            }
            int16_t sample_l = (int16_t)(sample_l_24bit >> 8);

            int32_t sample_r_24bit = (raw_r >> 8) & 0x00FFFFFF;
            if (sample_r_24bit & 0x00800000) {
                sample_r_24bit |= 0xFF000000;
            }
            int16_t sample_r = (int16_t)(sample_r_24bit >> 8);

            // Sum both channels (this works regardless of which channel has the mic signal)
            int32_t sample_sum = sample_l + sample_r;

            // Convert back to 16-bit range
            int16_t sample = constrain(sample_sum, INT16_MIN, INT16_MAX);

            if (sample != 0) has_non_zero = true;
            if (sample < min_sample) min_sample = sample;
            if (sample > max_sample) max_sample = sample;
            sum_abs += (sample < 0) ? -sample : sample;

            if (_input_gain != 1.0f) {
                int32_t scaled = static_cast<int32_t>(sample * _input_gain);
                sample = constrain(scaled, INT16_MIN, INT16_MAX);
            }

            buffer[i] = sample;
        }

    if (read_count % 100 == 0) {
        int32_t avg_abs = (stereo_pairs > 0) ? (sum_abs / stereo_pairs) : 0;
        ESP_LOGD(TAG, "[Mic Stats] t=%lu min=%d, max=%d, avg_abs=%d, has_non_zero=%s, samples=%d",
                 millis(), min_sample, max_sample, avg_abs, has_non_zero ? "YES" : "NO", stereo_pairs);
    }

    // No free() needed - buffer is pre-allocated and reused (zero-churn optimization)
    _last_input_time = millis();
    return stereo_pairs;
}

int AudioCodec::getActualInputSampleRate() const {
    // Full-duplex I2S: hardware runs at speaker rate (24kHz)
    return _output_sample_rate;
}

int AudioCodec::write(const int16_t* buffer, int samples) {
    static int write_count = 0;
    write_count++;

    if (!_initialized) {
        ESP_LOGW(TAG, "🟡 Write failed: codec not initialized");
        return 0;
    }
    if (!_spk_initialized) {
        ESP_LOGW(TAG, "🟡 Write failed: speaker not initialized");
        return 0;
    }
    if (!buffer) {
        ESP_LOGW(TAG, "🟡 Write failed: null buffer");
        return 0;
    }
    if (_muted) {
        return 0;
    }

    // Auto-enable output if it was disabled
    if (!_output_enabled) {
        ESP_LOGW(TAG, "🟡 Write called but output disabled! Auto-enabling... (input enabled: %d)", _input_enabled);
        enableOutput(true);
    }

    if (write_count == 1) {
        ESP_LOGI(TAG, "[Zero-Churn] Using pre-allocated PSRAM buffer @ %p", _i2s_write_buffer);
    }

    // Bounds check: ensure samples fit in pre-allocated buffer
    if (samples > _max_buffer_samples) {
        ESP_LOGW(TAG, "Write samples (%d) exceeds buffer size (%d), clamping",
                 samples, _max_buffer_samples);
        samples = _max_buffer_samples;
    }

    // Full-duplex stereo mode: duplicate mono to both L+R channels
    size_t stereo_size = samples * 2 * sizeof(int32_t);

    // Use pre-allocated PSRAM buffer (zero-churn optimization)
    // Apply volume and duplicate to both channels
    for (int i = 0; i < samples; i++) {
        int32_t temp = (buffer[i] * _output_volume) / 100;
        int16_t sample = constrain(temp, INT16_MIN, INT16_MAX);
        int32_t sample_32 = ((int32_t)sample) << 16;

        _i2s_write_buffer[i * 2] = sample_32;      // LEFT
        _i2s_write_buffer[i * 2 + 1] = sample_32;  // RIGHT (duplicate)
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_write(I2S_NUM_FULLDUPLEX, _i2s_write_buffer, stereo_size,
                              &bytes_written, portMAX_DELAY);

    // No free() needed - buffer is pre-allocated and reused (zero-churn optimization)

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ I2S write failed: %s", esp_err_to_name(ret));
        return 0;
    }

    _last_output_time = millis();
    return bytes_written / (2 * sizeof(int32_t));
}

// Power management timer callback
void AudioCodec::powerTimerCallback(void* arg) {
    AudioCodec* codec = static_cast<AudioCodec*>(arg);
    codec->checkAndUpdatePowerState();
}

// Keep output alive (reset timer to prevent auto-disable during listening)
void AudioCodec::keepOutputAlive() {
    if (_output_enabled) {
        _last_output_time = millis();
        ESP_LOGD(TAG, "Output timer reset (keeping speaker alive)");
    }
}

// Check and update power state based on inactivity
void AudioCodec::checkAndUpdatePowerState() {
    unsigned long now = millis();
    unsigned long input_elapsed = now - _last_input_time;

    // ⚠️ QUICK TEST: Power management TEMPORARILY DISABLED for debugging
    // This will help identify if power management is causing mic issues during playback

    // Auto-disable input after timeout (power saving for microphone)
    // DISABLED FOR TESTING
    // if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && _input_enabled) {
    //     ESP_LOGI(TAG, "Auto-disabling microphone after %lu ms inactivity", input_elapsed);
    //     enableInput(false);
    // }

    ESP_LOGD(TAG, "⚠️ TEST MODE: Power mgmt disabled (input elapsed: %lu ms, enabled: %d, output enabled: %d)",
             input_elapsed, _input_enabled, _output_enabled);

    // Note: Output (speaker) is NOT auto-disabled to ensure immediate TTS playback
    // The speaker stays enabled once started to avoid delays when TTS begins

    // Stop timer if input is disabled (output stays enabled, so timer continues)
    // DISABLED FOR TESTING
    // if (!_input_enabled && !_output_enabled && _power_timer) {
    //     esp_timer_stop(_power_timer);
    //     ESP_LOGD(TAG, "Power timer stopped (both channels idle)");
    // }
}
