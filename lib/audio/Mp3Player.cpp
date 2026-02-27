/**
 * Mp3Player.cpp
 *
 * Implementation for Mp3Player.
 */

#include "Mp3Player.h"
#include "Mp3Decoder.h"
#include "LittlefsManager.h"
#include "TaskManager.h"
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "Mp3Player";

Mp3Player::Mp3Player(AudioCodec* codec, LittleFSManager* filesystem)
    : _codec(codec)
    , _filesystem(filesystem)
    , _is_playing(false)
    , _stop_requested(false)
{
    _playback_data.mp3_buffer = nullptr;
    _playback_data.mp3_size = 0;
    _playback_data.path = nullptr;
    _playback_data.owns_buffer = false;
    _playback_data.buffer_in_psram = false;
    _pcm_buffer = nullptr;
    _decoder_ready = false;
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        _cache[i].path[0] = '\0';
        _cache[i].buffer = nullptr;
        _cache[i].size = 0;
        _cache[i].last_used_ms = 0;
        _cache[i].in_use = false;
        _cache[i].in_psram = false;
    }
    _codec_mutex = xSemaphoreCreateMutex();
    ESP_LOGD(TAG, "Mp3Player created");
}

Mp3Player::~Mp3Player() {
    ESP_LOGD(TAG, "Mp3Player destructor");
    // Stop task via TaskManager (SELF_DELETING pattern)
    TaskManager::instance().stopTask("mp3_play");
    if (_playback_data.mp3_buffer && _playback_data.owns_buffer) {
        freeMp3Buffer(_playback_data.mp3_buffer, _playback_data.buffer_in_psram);
    }
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (_cache[i].in_use) {
            releaseCacheEntry(&_cache[i]);
        }
    }
    if (_decoder_ready) {
        MP3Decoder_FreeBuffers();
        _decoder_ready = false;
    }
    if (_pcm_buffer) {
        free(_pcm_buffer);
        _pcm_buffer = nullptr;
    }
    if (_codec_mutex) vSemaphoreDelete(_codec_mutex);
}

bool Mp3Player::ensureDecoderBuffers() {
    if (_decoder_ready) {
        return true;
    }
    if (!MP3Decoder_AllocateBuffers()) {
        ESP_LOGE(TAG, "❌ Failed to allocate MP3 decoder buffers");
        return false;
    }
    _decoder_ready = true;
    return true;
}

bool Mp3Player::ensurePcmBuffer() {
    if (_pcm_buffer) {
        return true;
    }
    _pcm_buffer = static_cast<int16_t*>(
        malloc(m_MAX_NSAMP * m_MAX_NCHAN * sizeof(int16_t)));
    if (!_pcm_buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate PCM buffer");
        return false;
    }
    return true;
}

uint8_t* Mp3Player::allocateMp3Buffer(size_t size, bool prefer_psram, bool* used_psram) {
    if (used_psram) {
        *used_psram = false;
    }
    if (prefer_psram) {
        uint8_t* buffer = static_cast<uint8_t*>(
            heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
        if (buffer) {
            if (used_psram) {
                *used_psram = true;
            }
            return buffer;
        }
    }
    return static_cast<uint8_t*>(malloc(size));
}

void Mp3Player::freeMp3Buffer(uint8_t* buffer, bool in_psram) {
    if (!buffer) {
        return;
    }
    if (in_psram) {
        heap_caps_free(buffer);
        return;
    }
    free(buffer);
}

Mp3Player::CacheEntry* Mp3Player::findCacheEntry(const char* path) {
    if (!path) {
        return nullptr;
    }
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (_cache[i].in_use && strcmp(_cache[i].path, path) == 0) {
            return &_cache[i];
        }
    }
    return nullptr;
}

Mp3Player::CacheEntry* Mp3Player::allocateCacheEntry() {
    CacheEntry* free_slot = nullptr;
    CacheEntry* oldest = nullptr;
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        if (!_cache[i].in_use) {
            free_slot = &_cache[i];
            break;
        }
        if (!oldest || _cache[i].last_used_ms < oldest->last_used_ms) {
            oldest = &_cache[i];
        }
    }

    if (free_slot) {
        return free_slot;
    }
    if (oldest) {
        releaseCacheEntry(oldest);
        return oldest;
    }
    return nullptr;
}

void Mp3Player::releaseCacheEntry(CacheEntry* entry) {
    if (!entry) {
        return;
    }
    if (entry->buffer) {
        freeMp3Buffer(entry->buffer, entry->in_psram);
    }
    entry->buffer = nullptr;
    entry->size = 0;
    entry->last_used_ms = 0;
    entry->path[0] = '\0';
    entry->in_use = false;
    entry->in_psram = false;
}

bool Mp3Player::playFile(const char* path) {
    ESP_LOGI(TAG, "playFile() called: %s", path);
    
    if (!_codec || !_codec->isReady()) {
        ESP_LOGW(TAG, "🟡 Codec not ready");
        return false;
    }
    if (_codec->isMuted()) {
        ESP_LOGW(TAG, "🟡 Audio muted, skipping playback");
        return false;
    }

    if (TaskManager::instance().isTaskActive("mp3_play")) {
        ESP_LOGW(TAG, "🟡 Already playing, skipping");
        return false;
    }
    _is_playing = true;
    _stop_requested = false;
    
    // Check available memory before starting. If decoder/PCM buffers already exist,
    // allow playback with low headroom to keep short loop sounds working.
    size_t free_mem = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_mem < 30720) {  // Require 30KB free when buffers are not allocated yet
        if (!_decoder_ready || !_pcm_buffer) {
            ESP_LOGE(TAG, "❌ Insufficient memory: %d bytes free (need 30KB)", free_mem);
            _is_playing = false;
            return false;
        }
        ESP_LOGW(TAG, "🟡 Low internal memory: %d bytes free, reusing buffers", free_mem);
    }
    
    // Open file and check size
    if (!_filesystem) {
        ESP_LOGE(TAG, "❌ Filesystem not available");
        _is_playing = false;
        return false;
    }
    File file = _filesystem->open(path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ Failed to open file");
        _is_playing = false;
        return false;
    }
    
    size_t file_size = file.size();
    if (file_size > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "❌ File too large: %d bytes (max %d)", file_size, MAX_FILE_SIZE);
        file.close();
        _is_playing = false;
        return false;
    }
    
    ESP_LOGD(TAG, "MP3 file size: %d bytes", file_size);

    CacheEntry* cached = findCacheEntry(path);
    if (cached && cached->size == file_size) {
        cached->last_used_ms = millis();
        _playback_data.mp3_buffer = cached->buffer;
        _playback_data.mp3_size = cached->size;
        _playback_data.path = cached->path;
        _playback_data.owns_buffer = false;
        _playback_data.buffer_in_psram = cached->in_psram;
        file.close();
    } else {
        if (cached && cached->size != file_size) {
            releaseCacheEntry(cached);
            cached = nullptr;
        }

        CacheEntry* cache_slot = nullptr;
        if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > file_size) {
            cache_slot = allocateCacheEntry();
        }

        bool cached_buffer = false;
        if (cache_slot) {
            bool used_psram = false;
            uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
            if (buffer && used_psram) {
                cache_slot->buffer = buffer;
                cache_slot->size = file_size;
                cache_slot->last_used_ms = millis();
                cache_slot->in_use = true;
                cache_slot->in_psram = true;
                strncpy(cache_slot->path, path, sizeof(cache_slot->path) - 1);
                cache_slot->path[sizeof(cache_slot->path) - 1] = '\0';
                cached_buffer = true;
            } else if (buffer) {
                freeMp3Buffer(buffer, used_psram);
            }
        }

        if (cached_buffer) {
            file.read(cache_slot->buffer, file_size);
            _playback_data.mp3_buffer = cache_slot->buffer;
            _playback_data.mp3_size = file_size;
            _playback_data.path = cache_slot->path;
            _playback_data.owns_buffer = false;
            _playback_data.buffer_in_psram = true;
            file.close();
        } else {
            bool used_psram = false;
            uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
            if (!buffer) {
                ESP_LOGE(TAG, "❌ Failed to allocate %d bytes", file_size);
                file.close();
                _is_playing = false;
                return false;
            }
            file.read(buffer, file_size);
            file.close();
            _playback_data.mp3_buffer = buffer;
            _playback_data.mp3_size = file_size;
            _playback_data.path = path;
            _playback_data.owns_buffer = true;
            _playback_data.buffer_in_psram = used_psram;
        }
    }
    
    ESP_LOGD(TAG, "Starting playback task (Core 1, Priority 2)");
    bool created = TaskManager::instance().createTask(
        "mp3_play",
        "Mp3Player",
        playbackTask,
        this,
        2,                      // Priority
        1,                      // Core 1
        6144,                   // 6KB stack
        CleanupPattern::SELF_DELETING,
        "MP3 audio file playback"
    );
    if (!created) {
        ESP_LOGE(TAG, "❌ Failed to create playback task");
        if (_playback_data.owns_buffer) {
            freeMp3Buffer(_playback_data.mp3_buffer, _playback_data.buffer_in_psram);
        }
        _playback_data.mp3_buffer = nullptr;
        _is_playing = false;
        return false;
    }
    
    ESP_LOGD(TAG, "Playback task started successfully");
    return true;
}

bool Mp3Player::preloadFile(const char* path) {
    if (!path || !_filesystem) {
        return false;
    }

    CacheEntry* cached = findCacheEntry(path);
    if (cached) {
        cached->last_used_ms = millis();
        return true;
    }

    File file = _filesystem->open(path, "r");
    if (!file) {
        return false;
    }

    size_t file_size = file.size();
    if (file_size > MAX_FILE_SIZE) {
        file.close();
        return false;
    }

    CacheEntry* cache_slot = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > file_size) {
        cache_slot = allocateCacheEntry();
    }
    if (!cache_slot) {
        file.close();
        return false;
    }

    bool used_psram = false;
    uint8_t* buffer = allocateMp3Buffer(file_size, true, &used_psram);
    if (!buffer || !used_psram) {
        if (buffer) {
            freeMp3Buffer(buffer, used_psram);
        }
        file.close();
        return false;
    }

    size_t read_bytes = file.read(buffer, file_size);
    file.close();
    if (read_bytes != file_size) {
        freeMp3Buffer(buffer, used_psram);
        return false;
    }

    cache_slot->buffer = buffer;
    cache_slot->size = file_size;
    cache_slot->last_used_ms = millis();
    cache_slot->in_use = true;
    cache_slot->in_psram = used_psram;
    strncpy(cache_slot->path, path, sizeof(cache_slot->path) - 1);
    cache_slot->path[sizeof(cache_slot->path) - 1] = '\0';
    return true;
}

void Mp3Player::playbackTask(void* param) {
    Mp3Player* p = (Mp3Player*)param;
    ESP_LOGD(TAG, "Playback task running on core %d", xPortGetCoreID());
    
    if (!p->ensureDecoderBuffers()) {
        if (p->_playback_data.owns_buffer) {
            p->freeMp3Buffer(p->_playback_data.mp3_buffer, p->_playback_data.buffer_in_psram);
        }
        p->_playback_data.mp3_buffer = nullptr;
        p->_is_playing = false;
        TaskManager::instance().markTaskStopped("mp3_play");
        vTaskDelete(nullptr);
        return;
    }
    
    if (xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "❌ Failed to acquire codec mutex");
        if (p->_playback_data.owns_buffer) {
            p->freeMp3Buffer(p->_playback_data.mp3_buffer, p->_playback_data.buffer_in_psram);
        }
        p->_playback_data.mp3_buffer = nullptr;
        p->_is_playing = false;
        TaskManager::instance().markTaskStopped("mp3_play");
        vTaskDelete(nullptr);
        return;
    }
    
    bool was_enabled = p->_codec->isOutputEnabled();
    if (!was_enabled) {
        ESP_LOGD(TAG, "Enabling codec output");
        p->_codec->enableOutput(true);
    }
    xSemaphoreGive(p->_codec_mutex);
    
    uint8_t* read_ptr = p->_playback_data.mp3_buffer;
    int32_t bytes_left = p->_playback_data.mp3_size;
    if (!p->ensurePcmBuffer()) {
        if (p->_playback_data.owns_buffer) {
            p->freeMp3Buffer(p->_playback_data.mp3_buffer, p->_playback_data.buffer_in_psram);
        }
        p->_playback_data.mp3_buffer = nullptr;
        p->_is_playing = false;
        TaskManager::instance().markTaskStopped("mp3_play");
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGD(TAG, "Starting MP3 decode loop");
    
    while (bytes_left > 0 && !p->_stop_requested) {
        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            break;
        }
        
        read_ptr += offset;
        bytes_left -= offset;
        
        int32_t bytes_before = bytes_left;
        int err = MP3Decode(read_ptr, &bytes_left, p->_pcm_buffer, 0);
        int32_t bytes_consumed = bytes_before - bytes_left;
        
        if (err) {
            if (err == ERR_MP3_INDATA_UNDERFLOW) {
                break;
            }
            read_ptr++;
            bytes_left--;
            continue;
        }
        
        read_ptr += bytes_consumed;
        
        int samples = MP3GetOutputSamps();
        int channels = MP3GetChannels();
        
        if (samples > 0 && channels > 0) {
            int mono_samples = samples / channels;
            
            if (channels == 2) {
                for (int i = 0; i < mono_samples; i++) {
                    p->_pcm_buffer[i] = (p->_pcm_buffer[i * 2] + p->_pcm_buffer[i * 2 + 1]) / 2;
                }
            }
            
            if (xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                p->_codec->write(p->_pcm_buffer, mono_samples);
                xSemaphoreGive(p->_codec_mutex);
            }
        }
        
        vTaskDelay(1);
    }
    
    ESP_LOGD(TAG, "Playback complete, cleaning up");
    
    if (xSemaphoreTake(p->_codec_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!was_enabled) {
            ESP_LOGD(TAG, "Disabling codec output");
            p->_codec->enableOutput(false);
        }
        xSemaphoreGive(p->_codec_mutex);
    }
    
    if (p->_playback_data.owns_buffer) {
        p->freeMp3Buffer(p->_playback_data.mp3_buffer, p->_playback_data.buffer_in_psram);
    }
    p->_playback_data.mp3_buffer = nullptr;
    p->_playback_data.mp3_size = 0;
    p->_playback_data.path = nullptr;
    p->_playback_data.owns_buffer = false;
    p->_playback_data.buffer_in_psram = false;
    p->_is_playing = false;
    p->_stop_requested = false;

    ESP_LOGD(TAG, "Playback task exiting");
    TaskManager::instance().markTaskStopped("mp3_play");
    vTaskDelete(nullptr);
}

void Mp3Player::stop() {
    if (_is_playing) {
        _stop_requested = true;
    }
}
