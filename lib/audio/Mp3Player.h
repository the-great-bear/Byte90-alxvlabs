/**
 * Mp3Player.h
 *
 * Declarations for Mp3Player.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Project includes
#include "AudioCodec.h"

// Forward declarations
/**
 * @brief LittleFSManager.
 */
class LittleFSManager;

/**
 * Mp3Player - MP3 file playback
 *
 * Features:
 * - MP3 file playback from filesystem
 * - Async playback in FreeRTOS task
 * - Thread-safe codec access
 *
 * Architecture:
 * - Loads entire MP3 file to memory (10KB limit)
 * - Decodes and plays in background task
 * - Uses semaphore for codec synchronization
 */
class Mp3Player {
public:
    /**
     * @brief Construct MP3 player instance
     *
     * @param codec Pointer to audio codec
     * @param filesystem Pointer to filesystem manager
     */
    Mp3Player(AudioCodec* codec, LittleFSManager* filesystem);

    /**
     * @brief Destroy MP3 player and cleanup resources
     */
    ~Mp3Player();

    /**
     * @brief Play MP3 file from filesystem
     *
     * @param path File path (must be .mp3 file)
     * @return true if playback started, false on error
     */
    bool playFile(const char* path);
    bool preloadFile(const char* path);
    void stop();
    bool isPlaying() const { return _is_playing; }

private:
    /**
     * @brief Background playback task (static entry point)
     *
     * @param param Pointer to Mp3Player instance
     */
    static void playbackTask(void* param);
    bool ensureDecoderBuffers();
    bool ensurePcmBuffer();
    uint8_t* allocateMp3Buffer(size_t size, bool prefer_psram, bool* used_psram);
    void freeMp3Buffer(uint8_t* buffer, bool in_psram);

    struct CacheEntry {
        char path[64];
        uint8_t* buffer;
        size_t size;
        uint32_t last_used_ms;
        bool in_use;
        bool in_psram;
    };

    CacheEntry* findCacheEntry(const char* path);
    CacheEntry* allocateCacheEntry();
    void releaseCacheEntry(CacheEntry* entry);

    // Audio output
    AudioCodec* _codec;

    // Filesystem access
    LittleFSManager* _filesystem;

    // FreeRTOS primitives
    SemaphoreHandle_t _codec_mutex;
    volatile bool _is_playing;
    volatile bool _stop_requested;

    // Playback data
    static const size_t MAX_FILE_SIZE = 10240;  // 10KB limit
    static const size_t CACHE_SLOTS = 4;

/**
 * @brief PlaybackData.
 */
    struct PlaybackData {
        uint8_t* mp3_buffer;
        size_t mp3_size;
        const char* path;
        bool owns_buffer;
        bool buffer_in_psram;
    } _playback_data;

    int16_t* _pcm_buffer;
    bool _decoder_ready;
    CacheEntry _cache[CACHE_SLOTS];
};
