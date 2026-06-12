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
      _sample_rate(sample_rate),
      _channels(channels),
      _frame_ms(frame_ms),
      _frame_size((sample_rate / 1000) * frame_ms),
      _capture_active(false),
      _capture_paused(false),
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
      _send_queue(nullptr),
      _state_mutex(nullptr),
      _pcm_buffer(nullptr),
      _opus_buffer(nullptr),
      _resample_buffer(nullptr),
      _resample_buffer_size(0),
      _openai_resample_buffer(nullptr),
      _openai_resample_buffer_size(0),
      _openai_resampler_ready(false),
      _pcm_playback_active(false),
      _pcm_playback_source(nullptr),
      _pcm_prime_ms(60)
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
    
    // Create encoder
    _encoder = new OpusEncoder(_sample_rate, _channels, _frame_ms);
    if (!_encoder->begin()) {
        ESP_LOGE(TAG, "❌ Failed to initialize Opus encoder");
        delete _encoder;
        _encoder = nullptr;
        return false;
    }
    
    // Allocate buffers in PSRAM (large, non-time-critical buffers)
    // PCM buffer sized for capture frames with headroom
    int max_frame_size = (_sample_rate / 1000) * _frame_ms * 10;
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

    // Free resampling buffer
    if (_resample_buffer != nullptr) {
        free(_resample_buffer);
        _resample_buffer = nullptr;
        _resample_buffer_size = 0;
    }

    // Stop PCM playback and free OpenAI resources
    stopPcmPlayback();
    if (_openai_resample_buffer != nullptr) {
        free(_openai_resample_buffer);
        _openai_resample_buffer = nullptr;
        _openai_resample_buffer_size = 0;
    }
    _openai_resampler_ready = false;

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

        // Debug: Calculate peak volume and log periodically (Matches OpenAIWebsocket)
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

                // No playback timestamps in OpenAI pipeline.
                packet.timestamp = 0;

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

// Forward declaration to avoid a circular dependency. The concrete realtime AI
// client (OpenAIWebsocket or GeminiWebsocket) is selected at build time; only
// popPcm() is needed here and both providers expose the same symbol/signature.
#if defined(AI_PROVIDER_GEMINI)
class GeminiWebsocket {
public:
    size_t popPcm(int16_t* out, size_t max_samples);
};
using RealtimePcmSource = GeminiWebsocket;
#else
class OpenAIWebsocket {
public:
    size_t popPcm(int16_t* out, size_t max_samples);
};
using RealtimePcmSource = OpenAIWebsocket;
#endif

bool AudioService::configureOpenAIResampler(int input_rate, int output_rate) {
    if (_openai_resampler_ready) {
        return true;
    }

    _openai_resampler.Configure(input_rate, output_rate);
    ESP_LOGI(TAG, "OpenAI capture resampler: %d Hz -> %d Hz", input_rate, output_rate);

    // Calculate max input samples for one frame
    int max_input_samples = (input_rate / 1000) * _frame_ms;
    _openai_resample_buffer_size = _openai_resampler.GetOutputSamples(max_input_samples);
    _openai_resample_buffer = static_cast<int16_t*>(
        malloc(_openai_resample_buffer_size * sizeof(int16_t)));

    if (!_openai_resample_buffer) {
        ESP_LOGW(TAG, "Failed to allocate OpenAI resample buffer");
        _openai_resampler_ready = false;
        return false;
    }

    ESP_LOGI(TAG, "Allocated OpenAI resample buffer: %d samples", _openai_resample_buffer_size);
    _openai_resampler_ready = true;
    return true;
}

int AudioService::processOpenAICapture(const int16_t* samples, int count, int16_t* output) {
    if (!_openai_resampler_ready || !output) {
        return 0;
    }

    // Resample to output rate
    _openai_resampler.Process(samples, count, output);
    int out_samples = _openai_resampler.GetOutputSamples(count);

    return out_samples;
}

bool AudioService::startPcmPlayback(void* source, int prime_ms) {
    if (_pcm_playback_active || !source) {
        return false;
    }

    _pcm_playback_source = source;
    _pcm_prime_ms = prime_ms;
    _pcm_playback_active = true;

    // Create PCM playback task via TaskManager
    bool created = TaskManager::instance().createTask(
        "pcm_playback",
        "AudioService",
        pcmPlaybackTask,
        this,
        5,                      // Priority (high, time-critical)
        1,                      // Core 1
        4096,                   // 4KB stack
        CleanupPattern::SELF_DELETING,
        "OpenAI Realtime PCM playback"
    );
    if (!created) {
        ESP_LOGE(TAG, "Failed to create PCM playback task");
        _pcm_playback_active = false;
        _pcm_playback_source = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "PCM playback started (prime: %d ms)", prime_ms);
    return true;
}

void AudioService::stopPcmPlayback() {
    _pcm_playback_active = false;
    _pcm_playback_source = nullptr;

    // TaskManager handles waiting for self-deleting task
    TaskManager::instance().stopTask("pcm_playback");

    if (_codec) {
        _codec->enableOutput(false);
    }

    ESP_LOGI(TAG, "PCM playback stopped");
}

void AudioService::pcmPlaybackTask(void* parameter) {
    AudioService* service = static_cast<AudioService*>(parameter);
    if (service) {
        service->pcmPlaybackTaskImpl();
    }
    TaskManager::instance().markTaskStopped("pcm_playback");
    vTaskDelete(nullptr);
}

void AudioService::pcmPlaybackTaskImpl() {
    if (!_codec || !_pcm_playback_source) {
        return;
    }

    RealtimePcmSource* client = static_cast<RealtimePcmSource*>(_pcm_playback_source);
    const int chunk_samples = 240;  // 10ms at 24kHz
    int16_t buffer[chunk_samples];

    // Calculate prime samples (e.g., 60ms at 24kHz = 1440 samples)
    const int sample_rate = _codec->getOutputSampleRate();
    const int prime_samples = (sample_rate / 1000) * _pcm_prime_ms;
    int primed_samples = 0;

    ESP_LOGD(TAG, "PCM playback task started (prime: %d samples)", prime_samples);

    while (_pcm_playback_active) {
        // Pop PCM from OpenAI client ring buffer
        size_t samples = client->popPcm(buffer, chunk_samples);

        if (samples == 0) {
            // No data available, small delay
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        // Prime buffer before starting playback
        if (primed_samples < prime_samples) {
            primed_samples += static_cast<int>(samples);
            if (primed_samples < prime_samples) {
                continue;  // Keep buffering
            }
            ESP_LOGD(TAG, "PCM playback primed (%d samples)", primed_samples);
        }

        if (OPENAI_TTS_GAIN != 1.0f) {
            // Apply OpenAI output gain to better match other pipelines.
            for (size_t i = 0; i < samples; i++) {
                int32_t scaled = static_cast<int32_t>(buffer[i] * OPENAI_TTS_GAIN);
                if (scaled > 32767) {
                    buffer[i] = 32767;
                } else if (scaled < -32768) {
                    buffer[i] = -32768;
                } else {
                    buffer[i] = static_cast<int16_t>(scaled);
                }
            }
        }

        // Call playback PCM callback for visualization
        if (_playback_pcm_callback) {
            _playback_pcm_callback(buffer, static_cast<int>(samples), _playback_pcm_callback_user_data);
        }

        // Write to codec
        _codec->enableOutput(true);
        _codec->write(buffer, static_cast<int>(samples));
    }

    ESP_LOGD(TAG, "PCM playback task ended");
}
