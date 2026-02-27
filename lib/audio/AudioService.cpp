/**
 * AudioService.cpp
 *
 * Implementation for AudioService.
 */

#include "AudioService.h"
#include "DeviceConfig.h"
#include "TaskManager.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <string.h>

static const char* TAG = "AudioService";

AudioService::AudioService(AudioCodec* codec, int sample_rate, int channels, int frame_ms)
    : _codec(codec),
      _encoder(nullptr),
      _decoder(nullptr),
      _sample_rate(sample_rate),
      _channels(channels),
      _frame_ms(frame_ms),
      _frame_size((sample_rate / 1000) * frame_ms),
      _capture_active(false),
      _capture_paused(false),
      _playback_active(false),
      _initialized(false),
      _opus_transmit_enabled(true),
      _backpressure_level(BackpressureLevel::NONE),
      _capture_callback(nullptr),
      _capture_callback_user_data(nullptr),
      _pcm_sample_callback(nullptr),
      _pcm_sample_callback_user_data(nullptr),
      _playback_pcm_callback(nullptr),
      _playback_pcm_callback_user_data(nullptr),
      _send_queue_full_callback(nullptr),
      _send_queue_full_callback_user_data(nullptr),
      _send_queue_full_last_ms(0),
      _opus_decode_queue(nullptr),
      _pcm_output_queue(nullptr),
      _send_queue(nullptr),
      _state_mutex(nullptr),
      _timestamp_queue(nullptr),
      _pcm_buffer(nullptr),
      _opus_buffer(nullptr),
      _resample_buffer(nullptr),
      _resample_buffer_size(0)
{
}

AudioService::~AudioService() {
    end();
}

bool AudioService::begin() {
    if (_initialized) {
        ESP_LOGW(TAG, "AudioService already initialized");
        return true;
    }
    
    if (_codec == nullptr || !_codec->isReady()) {
        ESP_LOGE(TAG, "AudioCodec not ready");
        return false;
    }
    
    // Configure input resampler if codec input rate differs from target rate
    int codec_input_rate = _codec->getActualInputSampleRate();  // Use actual hardware rate
    if (codec_input_rate != _sample_rate) {
        ESP_LOGD(TAG, "Configuring input resampler: %d Hz -> %d Hz", 
                 codec_input_rate, _sample_rate);
        _input_resampler.Configure(codec_input_rate, _sample_rate);
        
        // Allocate resampling buffer
        // Calculate max input samples needed for one frame
        int max_input_samples = (_frame_size * codec_input_rate) / _sample_rate;
        _resample_buffer_size = _input_resampler.GetOutputSamples(max_input_samples);
        _resample_buffer = (int16_t*)malloc(_resample_buffer_size * _channels * sizeof(int16_t));
        if (_resample_buffer == nullptr) {
            ESP_LOGE(TAG, "❌ Failed to allocate resampling buffer");
            end();
            return false;
        }
        ESP_LOGD(TAG, "Allocated resampling buffer: %d samples", _resample_buffer_size);
    }
    
    // Get codec output rate for decoder configuration
    int codec_output_rate = _codec->getOutputSampleRate();

    // Create encoder
    _encoder = new OpusEncoder(_sample_rate, _channels, _frame_ms);
    if (!_encoder->begin()) {
        ESP_LOGE(TAG, "❌ Failed to initialize Opus encoder");
        delete _encoder;
        _encoder = nullptr;
        return false;
    }
    
    // Create decoder (use codec's output sample rate for TTS playback)
    int decoder_sample_rate = codec_output_rate;
    
    // Configure output resampler if decoder rate differs from codec output rate
    // (Currently both use same rate, but kept for future flexibility)
    if (decoder_sample_rate != codec_output_rate) {
        ESP_LOGD(TAG, "Configuring output resampler: %d Hz -> %d Hz", 
                 decoder_sample_rate, codec_output_rate);
        _output_resampler.Configure(decoder_sample_rate, codec_output_rate);
    }
    
    ESP_LOGD(TAG, "Creating Opus decoder at %d Hz (codec output rate)", decoder_sample_rate);
    _decoder = new OpusDecoder(decoder_sample_rate, _channels);
    if (!_decoder->begin()) {
        ESP_LOGE(TAG, "❌ Failed to initialize Opus decoder");
        delete _decoder;
        _decoder = nullptr;
        delete _encoder;
        _encoder = nullptr;
        return false;
    }
    
    // Allocate buffers in PSRAM (large, non-time-critical buffers)
    // PCM buffer needs to handle both STT (16kHz) and TTS (24kHz)
    // Use decoder sample rate for buffer size since TTS playback may be higher
    int max_frame_size = (decoder_sample_rate / 1000) * _frame_ms * 10;  // 10x for burst decoding
    _pcm_buffer = (int16_t*)heap_caps_malloc(max_frame_size * _channels * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    _opus_buffer = (uint8_t*)heap_caps_malloc(AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE, MALLOC_CAP_SPIRAM);

    ESP_LOGD(TAG, "Allocated PCM buffer: %d samples (%.1f KB) in %s",
             max_frame_size, (max_frame_size * _channels * sizeof(int16_t)) / 1024.0,
             heap_caps_get_allocated_size(_pcm_buffer) > 0 ? "PSRAM" : "FAILED");
    ESP_LOGD(TAG, "Allocated Opus buffer: %d bytes in %s",
             AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE,
             heap_caps_get_allocated_size(_opus_buffer) > 0 ? "PSRAM" : "FAILED");
    
    if (_pcm_buffer == nullptr || _opus_buffer == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to allocate audio buffers");
        end();
        return false;
    }
    
    // Log memory status
    ESP_LOGD(TAG, "Memory after audio buffers: Internal=%dKB, PSRAM=%dKB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    
    // Create Opus decode queue (WebSocket -> Decoder)
    _opus_decode_queue = xQueueCreate(OPUS_DECODE_QUEUE_SIZE, sizeof(OpusPacket));
    if (_opus_decode_queue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create opus decode queue");
        end();
        return false;
    }

    // Create PCM output queue (Decoder -> I2S)
    _pcm_output_queue = xQueueCreate(PCM_OUTPUT_QUEUE_SIZE, sizeof(PcmPacket));
    if (_pcm_output_queue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create PCM output queue");
        end();
        return false;
    }

    // Create send queue (for encoded packets)
    _send_queue = xQueueCreate(SEND_QUEUE_SIZE, sizeof(OpusPacket));
    if (_send_queue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create send queue");
        end();
        return false;
    }

    // Create mutex for thread safety
    _state_mutex = xSemaphoreCreateMutex();
    if (_state_mutex == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create state mutex");
        end();
        return false;
    }

    // Create timestamp queue for server-side AEC
    _timestamp_queue = xQueueCreate(TIMESTAMP_QUEUE_SIZE, sizeof(uint32_t));
    if (_timestamp_queue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create timestamp queue");
        end();
        return false;
    }

    // Initialize ring buffer for PCM decode/resample (eliminates 781 KB/s memory churn)
    ESP_LOGD(TAG, "Allocating ring buffer (%d slots)...", PcmRingBuffer::RING_SIZE);
    int max_decode_samples = (decoder_sample_rate / 1000) * _frame_ms * 10;
    int max_resample_samples = codec_output_rate != decoder_sample_rate
        ? _output_resampler.GetOutputSamples(max_decode_samples)
        : max_decode_samples;

    for (int i = 0; i < PcmRingBuffer::RING_SIZE; i++) {
        _ring_buffer.slots[i].pcm_data = (int16_t*)heap_caps_malloc(
            max_decode_samples * _channels * sizeof(int16_t),
            MALLOC_CAP_SPIRAM);
        _ring_buffer.slots[i].resample_data = (int16_t*)heap_caps_malloc(
            max_resample_samples * _channels * sizeof(int16_t),
            MALLOC_CAP_SPIRAM);

        if (_ring_buffer.slots[i].pcm_data == nullptr || _ring_buffer.slots[i].resample_data == nullptr) {
            ESP_LOGE(TAG, "❌ Failed to allocate ring buffer slot %d", i);
            end();
            return false;
        }

        _ring_buffer.slots[i].samples = 0;
        _ring_buffer.slots[i].timestamp = 0;
        _ring_buffer.slots[i].needs_resample = false;
        _ring_buffer.slots[i].in_use = false;
    }

    _ring_buffer.write_idx = 0;
    _ring_buffer.read_idx = 0;
    _ring_buffer.full_sem = xSemaphoreCreateCounting(PcmRingBuffer::RING_SIZE, 0);  // 0 full slots initially
    _ring_buffer.empty_sem = xSemaphoreCreateCounting(PcmRingBuffer::RING_SIZE, PcmRingBuffer::RING_SIZE);  // All empty

    if (_ring_buffer.full_sem == nullptr || _ring_buffer.empty_sem == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create ring buffer semaphores");
        end();
        return false;
    }

    ESP_LOGD(TAG, "Ring buffer allocated: %d slots × %.1f KB = %.1f KB total PSRAM",
             PcmRingBuffer::RING_SIZE,
             (max_decode_samples + max_resample_samples) * _channels * sizeof(int16_t) / 1024.0,
             PcmRingBuffer::RING_SIZE * (max_decode_samples + max_resample_samples) * _channels * sizeof(int16_t) / 1024.0);

    _initialized = true;
    ESP_LOGI(TAG, "AudioService initialized: %d Hz, %d ch, %d ms frames",
             _sample_rate, _channels, _frame_ms);

    // Report detailed memory usage
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "Memory after AudioService init:");
    ESP_LOGI(TAG, "  Internal: %u KB used / %u KB total (%.1f%% free)",
             (internal_total - internal_free) / 1024, internal_total / 1024,
             (internal_free * 100.0f) / internal_total);
    ESP_LOGI(TAG, "  PSRAM: %u KB used / %u KB total (%.1f%% free)",
             (psram_total - psram_free) / 1024, psram_total / 1024,
             (psram_free * 100.0f) / psram_total);

    return true;
}

void AudioService::end() {
    stopCapture();
    stopPlayback();

    if (_opus_decode_queue != nullptr) {
        // Drain queue
        OpusPacket packet;
        while (xQueueReceive(_opus_decode_queue, &packet, 0) == pdTRUE) {
            // No need to free - data is inline
        }
        vQueueDelete(_opus_decode_queue);
        _opus_decode_queue = nullptr;
    }

    if (_pcm_output_queue != nullptr) {
        // Drain queue and free PCM buffers
        PcmPacket packet;
        while (xQueueReceive(_pcm_output_queue, &packet, 0) == pdTRUE) {
            if (packet.data != nullptr) {
                free(packet.data);
            }
        }
        vQueueDelete(_pcm_output_queue);
        _pcm_output_queue = nullptr;
    }

    if (_send_queue != nullptr) {
        // Drain queue
        OpusPacket packet;
        while (xQueueReceive(_send_queue, &packet, 0) == pdTRUE) {
            // No need to free - data is inline
        }
        vQueueDelete(_send_queue);
        _send_queue = nullptr;
    }

    if (_state_mutex != nullptr) {
        vSemaphoreDelete(_state_mutex);
        _state_mutex = nullptr;
    }

    if (_timestamp_queue != nullptr) {
        vQueueDelete(_timestamp_queue);
        _timestamp_queue = nullptr;
    }

    // Cleanup ring buffer
    for (int i = 0; i < PcmRingBuffer::RING_SIZE; i++) {
        if (_ring_buffer.slots[i].pcm_data != nullptr) {
            free(_ring_buffer.slots[i].pcm_data);
            _ring_buffer.slots[i].pcm_data = nullptr;
        }
        if (_ring_buffer.slots[i].resample_data != nullptr) {
            free(_ring_buffer.slots[i].resample_data);
            _ring_buffer.slots[i].resample_data = nullptr;
        }
    }
    if (_ring_buffer.full_sem != nullptr) {
        vSemaphoreDelete(_ring_buffer.full_sem);
        _ring_buffer.full_sem = nullptr;
    }
    if (_ring_buffer.empty_sem != nullptr) {
        vSemaphoreDelete(_ring_buffer.empty_sem);
        _ring_buffer.empty_sem = nullptr;
    }

    if (_pcm_buffer != nullptr) {
        free(_pcm_buffer);
        _pcm_buffer = nullptr;
    }
    
    if (_opus_buffer != nullptr) {
        free(_opus_buffer);
        _opus_buffer = nullptr;
    }

    if (_encoder != nullptr) {
        delete _encoder;
        _encoder = nullptr;
    }

    if (_decoder != nullptr) {
        delete _decoder;
        _decoder = nullptr;
    }

    // Free resampling buffer
    if (_resample_buffer != nullptr) {
        free(_resample_buffer);
        _resample_buffer = nullptr;
        _resample_buffer_size = 0;
    }


    _initialized = false;
    ESP_LOGI(TAG, "AudioService ended");
}

bool AudioService::startCapture() {
    if (!_initialized) {
        ESP_LOGE(TAG, "AudioService not initialized");
        return false;
    }

    // Lock state mutex
    if (xSemaphoreTake(_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "❌ Failed to take state mutex");
        return false;
    }

    if (_capture_active) {
        xSemaphoreGive(_state_mutex);
        ESP_LOGW(TAG, "Capture already active");
        return true;
    }

    // Enable codec input
    _codec->enableInput(true);

    // Set active flag BEFORE creating task to avoid race condition
    _capture_active = true;

    xSemaphoreGive(_state_mutex);

    // Create capture task via TaskManager
    bool created = TaskManager::instance().createTask(
        "audio_capture",
        "AudioService",
        captureTask,
        this,
        5,                      // Priority
        0,                      // Core 0
        24576,                  // 24KB stack - needs space for Opus encoding
        CleanupPattern::GRACEFUL,
        "Captures microphone audio and encodes to Opus"
    );
    if (!created) {
        ESP_LOGE(TAG, "❌ Failed to create capture task");
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _capture_active = false;
        xSemaphoreGive(_state_mutex);
        _codec->enableInput(false);
        return false;
    }

    ESP_LOGI(TAG, "Audio capture started (priority 5)");
    return true;
}

void AudioService::stopCapture() {
    // Lock state mutex
    if (xSemaphoreTake(_state_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if (!_capture_active) {
        xSemaphoreGive(_state_mutex);
        return;
    }

    _capture_active = false;
    xSemaphoreGive(_state_mutex);

    // Stop I2S first to unblock i2s_read() in the task
    // This allows the task to exit gracefully
    ESP_LOGD(TAG, "Disabling I2S input");
    _codec->enableInput(false);

    // TaskManager handles graceful shutdown with polling
    TaskManager::instance().stopTask("audio_capture");

    ESP_LOGI(TAG, "Audio capture stopped");
}

bool AudioService::startPlayback() {
    if (!_initialized) {
        ESP_LOGE(TAG, "AudioService not initialized");
        return false;
    }

    // Lock state mutex
    if (xSemaphoreTake(_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "❌ Failed to take state mutex");
        return false;
    }

    if (_playback_active) {
        xSemaphoreGive(_state_mutex);
        ESP_LOGW(TAG, "🟡 Playback already active");
        return true;
    }

    // Enable codec output
    _codec->enableOutput(true);

    // Set active flag BEFORE creating task to avoid race condition
    _playback_active = true;

    xSemaphoreGive(_state_mutex);

    // Create Opus decode task via TaskManager
    // Priority 2 - Low, for CPU-intensive decoding
    // Pinned to Core 1 (output path - network to speaker, paired with output task)
    bool decode_created = TaskManager::instance().createTask(
        "opus_decode",
        "AudioService",
        opusDecodeTask,
        this,
        2,                      // Priority
        1,                      // Core 1
        24576,                  // 24KB stack
        CleanupPattern::GRACEFUL,
        "Decodes incoming Opus audio packets for playback"
    );
    if (!decode_created) {
        ESP_LOGE(TAG, "❌ Failed to create opus decode task");
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _playback_active = false;
        xSemaphoreGive(_state_mutex);
        _codec->enableOutput(false);
        return false;
    }

    // Create audio output task via TaskManager
    // Priority 5 - Medium-High, for time-critical I2S writes
    // Pinned to Core 1 (output path - paired with decode task for cache efficiency)
    bool output_created = TaskManager::instance().createTask(
        "audio_output",
        "AudioService",
        audioOutputTask,
        this,
        5,                      // Priority
        1,                      // Core 1
        4096,                   // 4KB stack (reduced to save memory)
        CleanupPattern::GRACEFUL,
        "Handles I2S output for speaker/headphone playback"
    );
    if (!output_created) {
        ESP_LOGE(TAG, "❌ Failed to create audio output task");
        TaskManager::instance().stopTask("opus_decode");
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _playback_active = false;
        xSemaphoreGive(_state_mutex);
        _codec->enableOutput(false);
        return false;
    }

    ESP_LOGI(TAG, "Audio playback started (decode: priority 2, output: priority 5)");
    return true;
}

void AudioService::stopPlayback() {
    ESP_LOGD(TAG, "stopPlayback() called from core %d", xPortGetCoreID());
    
    // Lock state mutex
    if (xSemaphoreTake(_state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "❌ Failed to take state mutex in stopPlayback()");
        return;
    }

    if (!_playback_active) {
        ESP_LOGD(TAG, "Playback already stopped, returning");
        xSemaphoreGive(_state_mutex);
        return;
    }

    ESP_LOGD(TAG, "Setting _playback_active = false");
    _playback_active = false;
    xSemaphoreGive(_state_mutex);

    // Stop I2S first to unblock any I2S operations in tasks
    ESP_LOGD(TAG, "Disabling I2S output");
    _codec->enableOutput(false);
    
    // Drain the PCM output queue to unblock decode task if it's trying to send
    ESP_LOGD(TAG, "Draining PCM output queue");
    PcmPacket drainPacket;
    int drained = 0;
    while (xQueueReceive(_pcm_output_queue, &drainPacket, 0) == pdTRUE) {
        if (drainPacket.data != nullptr) {
            free(drainPacket.data);
        }
        drained++;
    }
    if (drained > 0) {
        ESP_LOGD(TAG, "Drained %d packets from PCM output queue", drained);
    }

    // TaskManager handles graceful shutdown with polling for both tasks
    ESP_LOGD(TAG, "Stopping playback tasks via TaskManager");
    TaskManager::instance().stopTask("opus_decode");
    TaskManager::instance().stopTask("audio_output");

    ESP_LOGI(TAG, "Audio playback stopped successfully");
}

void AudioService::setCaptureCallback(AudioPacketCallback callback, void* user_data) {
    _capture_callback = callback;
    _capture_callback_user_data = user_data;
}

void AudioService::setPcmSampleCallback(PcmSampleCallback callback, void* user_data) {
    _pcm_sample_callback = callback;
    _pcm_sample_callback_user_data = user_data;
}

void AudioService::setPlaybackPcmCallback(PcmSampleCallback callback, void* user_data) {
    _playback_pcm_callback = callback;
    _playback_pcm_callback_user_data = user_data;
}

void AudioService::setSendQueueFullCallback(SendQueueFullCallback callback, void* user_data) {
    _send_queue_full_callback = callback;
    _send_queue_full_callback_user_data = user_data;
}

void AudioService::setOpusTransmitEnabled(bool enabled) {
    _opus_transmit_enabled = enabled;
}

void AudioService::notifySendQueueFull() {
    if (!_send_queue_full_callback) {
        return;
    }

    uint32_t now = millis();
    const uint32_t burst_gap_ms = 500;
    if (_send_queue_full_last_ms == 0 ||
        (now - _send_queue_full_last_ms) >= burst_gap_ms) {
        _send_queue_full_last_ms = now;
        _send_queue_full_callback(_send_queue_full_callback_user_data);
    }
}

void AudioService::setCapturePaused(bool paused) {
    _capture_paused = paused;
    if (_codec) {
        _codec->enableInput(!paused);
    }
}

bool AudioService::queuePlaybackPacket(const uint8_t* data, int len, uint32_t timestamp) {
    if (!_playback_active || _opus_decode_queue == nullptr) {
        return false;
    }

    if (len <= 0 || len > AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE) {
        return false;
    }

    // Create Opus packet for decode queue
    OpusPacket packet;
    memcpy(packet.data, data, len);
    packet.len = len;
    packet.timestamp = timestamp;

    if (xQueueSend(_opus_decode_queue, &packet, 0) != pdTRUE) {
        return false;
    }

    return true;
}

void AudioService::setBitrate(int bitrate) {
    if (_encoder != nullptr) {
        _encoder->setBitrate(bitrate);
    }
}

void AudioService::setComplexity(int complexity) {
    if (_encoder != nullptr) {
        _encoder->setComplexity(complexity);
    }
}

void AudioService::captureTask(void* parameter) {
    AudioService* service = static_cast<AudioService*>(parameter);
    service->captureTaskImpl();
    TaskManager::instance().markTaskStopped("audio_capture");
    vTaskDelete(nullptr);
}

void AudioService::captureTaskImpl() {
    // Verify we're running on Core 0
    BaseType_t core_id = xPortGetCoreID();
    ESP_LOGD(TAG, "Audio capture task started (Priority 8, Core %d, expected: 0)", core_id);
    
    int codec_input_rate = _codec->getActualInputSampleRate();
    int target_rate = _sample_rate;
    bool needs_resampling = (codec_input_rate != target_rate);

    while (_capture_active) {
        if (_capture_paused) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        // Calculate how many samples to read from codec
        int samples_to_read;
        if (needs_resampling) {
            // Need to read more samples to account for downsampling
            samples_to_read = (_frame_size * codec_input_rate) / target_rate;
        } else {
            samples_to_read = _frame_size;
        }
        
        // Read PCM from codec at hardware rate
        int samples_read = _codec->read(_pcm_buffer, samples_to_read);

        // Check if we should exit (I2S might have been stopped)
        if (!_capture_active) {
            break;
        }

        if (samples_read < samples_to_read) {
            // Incomplete read might be due to I2S being stopped
            if (!_capture_active) {
                break;
            }
            ESP_LOGW(TAG, "Incomplete frame read: %d/%d", samples_read, samples_to_read);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Resample if needed
        int16_t* encode_buffer = _pcm_buffer;
        int encode_samples = samples_read;
        
        if (needs_resampling && _resample_buffer != nullptr) {
            int output_samples = _input_resampler.GetOutputSamples(samples_read);
            if (output_samples > _resample_buffer_size) {
                ESP_LOGE(TAG, "Resampling buffer too small: need %d, have %d", 
                         output_samples, _resample_buffer_size);
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            
            _input_resampler.Process(_pcm_buffer, samples_read, _resample_buffer);
            encode_buffer = _resample_buffer;
            encode_samples = output_samples;
        }

        // Debug: Calculate peak volume and log periodically.
        static uint32_t last_vol_log = 0;
        static int16_t max_vol = 0;
        static int clip_count = 0;
        int16_t frame_peak = 0;
        constexpr int16_t CLIP_THRESHOLD = 32760;
        for (int i = 0; i < encode_samples; i++) {
            int16_t v = abs(encode_buffer[i]);
            if (v > max_vol) max_vol = v;
            if (v > frame_peak) frame_peak = v;
            if (v >= CLIP_THRESHOLD) {
                clip_count++;
            }
        }

        uint32_t now = millis();
        if (now - last_vol_log > 500) {
            float clip_pct = (encode_samples > 0)
                                 ? (clip_count * 100.0f / encode_samples)
                                 : 0.0f;
            ESP_LOGI(TAG, "[Mic Vol] Peak: %d, Clip: %d (%.2f%%)",
                     max_vol, clip_count, clip_pct);
            max_vol = 0;
            clip_count = 0;
            last_vol_log = now;
        }

        // Call PCM sample callback for visualization (RMS-based, non-blocking)
        if (_pcm_sample_callback != nullptr) {
            _pcm_sample_callback(encode_buffer, encode_samples, _pcm_sample_callback_user_data);
        }

        if (!_opus_transmit_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        static constexpr float SOFT_THRESHOLD_PCT = 70.0f;
        static constexpr float HARD_THRESHOLD_PCT = 90.0f;
        static constexpr uint32_t SOFT_BACKPRESSURE_DELAY_MS = 150;
        static constexpr uint32_t HARD_BACKPRESSURE_DELAY_MS = 700;

        if (_send_queue != nullptr) {
            UBaseType_t queued = uxQueueMessagesWaiting(_send_queue);
            float fill_pct = (queued * 100.0f) / SEND_QUEUE_SIZE;
            BackpressureLevel new_level = BackpressureLevel::NONE;
            if (fill_pct >= HARD_THRESHOLD_PCT) {
                new_level = BackpressureLevel::HARD;
            } else if (fill_pct >= SOFT_THRESHOLD_PCT) {
                new_level = BackpressureLevel::SOFT;
            }

            if (new_level != _backpressure_level) {
                if (new_level == BackpressureLevel::NONE) {
                    ESP_LOGI(TAG, "[Backpressure] Recovered (queue %.1f%%)", fill_pct);
                } else if (new_level == BackpressureLevel::SOFT) {
                    ESP_LOGW(TAG, "[Backpressure] Soft throttle (queue %.1f%%)", fill_pct);
                } else {
                    ESP_LOGW(TAG, "[Backpressure] Hard throttle (queue %.1f%%)", fill_pct);
                }
                _backpressure_level = new_level;
            }

            if (_backpressure_level == BackpressureLevel::SOFT) {
                vTaskDelay(pdMS_TO_TICKS(SOFT_BACKPRESSURE_DELAY_MS));
                continue;
            }
            if (_backpressure_level == BackpressureLevel::HARD) {
                vTaskDelay(pdMS_TO_TICKS(HARD_BACKPRESSURE_DELAY_MS));
                continue;
            }
        }

        // Encode to Opus (using resampled data if resampling occurred)
        int encoded_size = _encoder->encode(encode_buffer, encode_samples, _opus_buffer, AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE);

        if (encoded_size > 0) {
            // Skip DTX packets (size <= 2)
            if (encoded_size > 2) {
                // Push to send queue (matches original architecture)
                OpusPacket packet;
                memcpy(packet.data, _opus_buffer, encoded_size);
                packet.len = encoded_size;

                // Attach timestamp from timestamp queue for server-side AEC
                packet.timestamp = 0;  // Default no timestamp
                if (_timestamp_queue != nullptr) {
                    xQueueReceive(_timestamp_queue, &packet.timestamp, 0);  // Non-blocking
                }

                // Monitor send queue fill level for early warning
                UBaseType_t queued = uxQueueMessagesWaiting(_send_queue);
                float fill_pct = (queued * 100.0f) / SEND_QUEUE_SIZE;

                static uint32_t last_warning_log = 0;
                static int warning_count = 0;

                if (fill_pct > 90.0f) {
                    // CRITICAL: Queue nearly full
                    if (millis() - last_warning_log > 2000) {
                        ESP_LOGW(TAG, "🔴 Send queue critical: %u/%u packets (%.1f%% full) - network slow!",
                                 queued, SEND_QUEUE_SIZE, fill_pct);
                        last_warning_log = millis();
                        warning_count = 0;
                    }
                } else if (fill_pct > 70.0f) {
                    // WARNING: Queue filling up
                    if (millis() - last_warning_log > 5000) {
                        ESP_LOGW(TAG, "🟠 Send queue elevated: %u/%u packets (%.1f%% full)",
                                 queued, SEND_QUEUE_SIZE, fill_pct);
                        last_warning_log = millis();
                    }
                } else if (fill_pct > 50.0f) {
                    // INFO: Moderate usage
                    if (++warning_count % 100 == 0 && millis() - last_warning_log > 10000) {
                        ESP_LOGI(TAG, "Send queue moderate: %u/%u packets (%.1f%% full)",
                                 queued, SEND_QUEUE_SIZE, fill_pct);
                        last_warning_log = millis();
                        warning_count = 0;
                    }
                }

                if (xQueueSend(_send_queue, &packet, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Send queue full, dropping packet");
                    notifySendQueueFull();
                } else {
                    // Notify via callback that packet is available
                    if (_capture_callback != nullptr) {
                        _capture_callback(nullptr, 0, _capture_callback_user_data);
                    }
                }
            } else {
                ESP_LOGD(TAG, "[AUDIO CAPTURE] DTX packet (silence), size: %d", encoded_size);
            }
        } else if (encoded_size < 0) {
            ESP_LOGE(TAG, "Encode error: %d", encoded_size);
        }
        
        // Small delay to prevent CPU spinning
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGD(TAG, "Capture task ended");
}


// Opus decode task (Priority 2 - decodes Opus packets to PCM)
void AudioService::opusDecodeTask(void* parameter) {
    AudioService* service = static_cast<AudioService*>(parameter);
    service->opusDecodeTaskImpl();
    TaskManager::instance().markTaskStopped("opus_decode");
    vTaskDelete(nullptr);
}

void AudioService::opusDecodeTaskImpl() {
    // Verify we're running on Core 1
    BaseType_t core_id = xPortGetCoreID();
    ESP_LOGD(TAG, "Opus decode task started (Priority 2, Core %d, expected: 1, %d Hz, %d ch)",
             core_id, _decoder->getSampleRate(), _decoder->getChannels());

    // Calculate max decode samples
    int decoder_sample_rate = _decoder->getSampleRate();
    int codec_output_rate = _codec->getOutputSampleRate();

    OpusPacket opusPacket;

    // Debug: track ring buffer statistics
    static uint32_t packets_processed = 0;
    static uint32_t last_log_time = 0;

    while (_playback_active) {
        // Wait for Opus packet from WebSocket
        if (xQueueReceive(_opus_decode_queue, &opusPacket, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (opusPacket.len > 0) {
                // Debug: Log decode activity
                packets_processed++;
                if (packets_processed % 10 == 0) {
                    ESP_LOGD(TAG, "Decoding packet %lu, size: %d bytes", packets_processed, opusPacket.len);
                }

                // Wait for empty slot (blocks if ring full - provides backpressure)
                if (xSemaphoreTake(_ring_buffer.empty_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
                    ESP_LOGW(TAG, "Ring buffer full, dropping packet");
                    continue;
                }

                // Get slot from ring buffer (NO malloc - uses pre-allocated buffer)
                int slot_idx = _ring_buffer.write_idx;
                PcmRingBuffer::Slot* slot = &_ring_buffer.slots[slot_idx];

                // Decode directly into ring buffer slot (NO malloc)
                int decoded_samples = _decoder->decode(
                    opusPacket.data, opusPacket.len,
                    slot->pcm_data, (decoder_sample_rate / 1000) * _frame_ms * 10);

                if (decoded_samples > 0) {
                    slot->samples = decoded_samples;
                    slot->timestamp = opusPacket.timestamp;
                    slot->needs_resample = (decoder_sample_rate != codec_output_rate);

                    // Resample if needed (directly into ring buffer slot, NO malloc)
                    if (slot->needs_resample) {
                        int resampled_size = _output_resampler.GetOutputSamples(decoded_samples);
                        _output_resampler.Process(slot->pcm_data, decoded_samples, slot->resample_data);
                        slot->samples = resampled_size;
                    }

                    slot->in_use = true;

                    // Advance write index (wrap around)
                    _ring_buffer.write_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;

                    // Signal full slot available
                    xSemaphoreGive(_ring_buffer.full_sem);

                    // Debug: track statistics
                    packets_processed++;
                    if (millis() - last_log_time > 30000) {  // Every 30 seconds
                        int slots_in_use = 0;
                        for (int i = 0; i < PcmRingBuffer::RING_SIZE; i++) {
                            if (_ring_buffer.slots[i].in_use) slots_in_use++;
                        }
                        ESP_LOGD(TAG, "Ring buffer stats: %lu packets processed, %d/%d slots in use (malloc rate: 0/sec, churn: 0 KB/s)",
                                 packets_processed, slots_in_use, PcmRingBuffer::RING_SIZE);
                        packets_processed = 0;
                        last_log_time = millis();
                    }
                } else {
                    // Decode failed, return slot
                    if (decoded_samples < 0) {
                        ESP_LOGE(TAG, "Decode error: %d", decoded_samples);
                    }
                    xSemaphoreGive(_ring_buffer.empty_sem);
                }
            }
        }
    }

    ESP_LOGD(TAG, "Opus decode task ended");
}

// Audio output task (Priority 4 - writes PCM to I2S)
void AudioService::audioOutputTask(void* parameter) {
    AudioService* service = static_cast<AudioService*>(parameter);
    service->audioOutputTaskImpl();
    TaskManager::instance().markTaskStopped("audio_output");
    vTaskDelete(nullptr);
}

void AudioService::audioOutputTaskImpl() {
    // Verify we're running on Core 1
    BaseType_t core_id = xPortGetCoreID();
    ESP_LOGD(TAG, "Audio output task started (Priority 4, Core %d, expected: 1)", core_id);

    while (_playback_active) {
        // Wait for full slot (blocks if ring empty - natural flow control)
        if (xSemaphoreTake(_ring_buffer.full_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        // Get slot from ring buffer
        int slot_idx = _ring_buffer.read_idx;
        PcmRingBuffer::Slot* slot = &_ring_buffer.slots[slot_idx];

        // Determine which buffer to use (resampled or original)
        int16_t* output_data = slot->needs_resample ? slot->resample_data : slot->pcm_data;

        // Call playback PCM callback for visualization (RMS-based, non-blocking)
        if (_playback_pcm_callback != nullptr) {
            _playback_pcm_callback(output_data, slot->samples, _playback_pcm_callback_user_data);
        }

        // Write PCM to I2S (time-critical, no logging)
        _codec->write(output_data, slot->samples);

        // Record timestamp for server-side AEC
        if (slot->timestamp > 0 && _timestamp_queue != nullptr) {
            if (xQueueSend(_timestamp_queue, &slot->timestamp, 0) != pdTRUE) {
                uint32_t old_timestamp;
                xQueueReceive(_timestamp_queue, &old_timestamp, 0);
                xQueueSend(_timestamp_queue, &slot->timestamp, 0);
            }
        }

        // Mark slot as empty (NO free - buffer is reused)
        slot->in_use = false;

        // Advance read index (wrap around)
        _ring_buffer.read_idx = (slot_idx + 1) % PcmRingBuffer::RING_SIZE;

        // Signal empty slot available
        xSemaphoreGive(_ring_buffer.empty_sem);
    }

    ESP_LOGD(TAG, "Audio output task ended");
}

// Pop packet from send queue for WebSocket transmission
bool AudioService::popPacketFromSendQueue(uint8_t* data, int* len, uint32_t* timestamp) {
    if (!_send_queue || !data || !len) {
        return false;
    }

    OpusPacket packet;
    if (xQueueReceive(_send_queue, &packet, 0) == pdTRUE) {
        memcpy(data, packet.data, packet.len);
        *len = packet.len;
        if (timestamp != nullptr) {
            *timestamp = packet.timestamp;
        }
        return true;
    }

    return false;
}
