/**
 * @file GifPlayer.cpp
 * @brief Implementation of AnimatedGIF playback functionality
 *
 * Provides functions for loading, initializing, and playing animated GIF files
 * from the filesystem using the AnimatedGIF library (bitbank2).
 */

#include "GifPlayer.h"
#include "ArduinoSSD1351.h"
#include "EffectsManager.h"
#include "TaskManager.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "GifPlayer";

static const char* gifErrorToString(int error) {
    switch (error) {
        case GIF_SUCCESS: return "GIF_SUCCESS";
        case GIF_DECODE_ERROR: return "GIF_DECODE_ERROR";
        case GIF_TOO_WIDE: return "GIF_TOO_WIDE";
        case GIF_INVALID_PARAMETER: return "GIF_INVALID_PARAMETER";
        case GIF_UNSUPPORTED_FEATURE: return "GIF_UNSUPPORTED_FEATURE";
        case GIF_FILE_NOT_OPEN: return "GIF_FILE_NOT_OPEN";
        case GIF_EARLY_EOF: return "GIF_EARLY_EOF";
        case GIF_EMPTY_FRAME: return "GIF_EMPTY_FRAME";
        case GIF_BAD_FILE: return "GIF_BAD_FILE";
        case GIF_ERROR_MEMORY: return "GIF_ERROR_MEMORY";
        default: return "GIF_UNKNOWN";
    }
}

// Static member initialization
File GifPlayer::_gifFile;
GifPlayer* GifPlayer::_instance = nullptr;

GifPlayer::GifPlayer(ArduinoSSD1351* display)
    : _display(display)
    , _effects_manager(nullptr)
    , _initialized(false)
    , _gif_mutex(nullptr)
    , _display_mutex(nullptr)
    , _dmaBuffer(nullptr)
    , _use_dma(true) // Enable DMA by default
    , _requested_gif("")
    , _current_gif("")
    , _task_running(false)
    , _should_loop(true)
    , _requested_loop(true)
    , _finished_once(false)
    , _next_frame_time_ms(0)
    , _frame_delay_ms(0)
    , _is_playing(false)
    , _last_error_restart_ms(0)
{
    _context.sharedFrameBuffer = nullptr;
    _context.offsetX = 0;
    _context.offsetY = 0;
    _instance = this;  // Set static instance for callbacks
}

GifPlayer::~GifPlayer() {
    // Stop task via TaskManager
    _task_running = false;
    TaskManager::instance().stopTask("gif_task");

    // Free mutex
    if (_gif_mutex) {
        vSemaphoreDelete(_gif_mutex);
        _gif_mutex = nullptr;
    }

    // Free DMA buffer
    if (_dmaBuffer) {
        heap_caps_free(_dmaBuffer);
        _dmaBuffer = nullptr;
    }

    stop();

    // Free the shared frame buffer upon destruction
    if (_context.sharedFrameBuffer) {
        heap_caps_free(_context.sharedFrameBuffer);
        _context.sharedFrameBuffer = nullptr;
    }

    if (_instance == this) {
        _instance = nullptr;
    }
}

bool GifPlayer::begin() {
    if (!_display) {
        ESP_LOGE(TAG, "Display pointer is null");
        return false;
    }

    ESP_LOGD(TAG, "Initializing GIF player");

    // Check memory status
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGD(TAG, "Free heap: %u bytes, Free PSRAM: %u bytes", freeHeap, freePSRAM);

    // Initialize AnimatedGIF library
    _gif.begin(GIF_PALETTE_RGB565_LE);

    // Allocate shared frame buffer in PSRAM
    // For full-canvas COOKED mode, we need space for the 8-bit canvas
    // AND the 16-bit "cooked" pixel buffer.
    // Total size = (width * height * 1) + (width * height * 2) = width * height * 3
    const size_t frameBufferSize = GIF_WIDTH * GIF_HEIGHT * 3;
    if (_context.sharedFrameBuffer == nullptr) {
        _context.sharedFrameBuffer = (uint8_t*)heap_caps_malloc(frameBufferSize, MALLOC_CAP_SPIRAM);
        if (!_context.sharedFrameBuffer) {
            ESP_LOGE(TAG, "Failed to allocate shared frame buffer (%d bytes)", frameBufferSize);
            return false;
        }
        // Zero out the buffer to prevent garbage data
        memset(_context.sharedFrameBuffer, 0, frameBufferSize);
        ESP_LOGD(TAG, "Frame buffer allocated: %d bytes in PSRAM (Full Canvas COOKED mode)", frameBufferSize);
    }

    // Allocate DMA-capable buffer for one scanline (GIF_WIDTH pixels * 2 bytes/pixel)
    _dmaBuffer = (uint16_t*)heap_caps_malloc(GIF_WIDTH * 2, MALLOC_CAP_DMA);
    if (!_dmaBuffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer (%d bytes)", GIF_WIDTH * 2);
        // Clean up other resources if this fails
        heap_caps_free(_context.sharedFrameBuffer);
        _context.sharedFrameBuffer = nullptr;
        return false;
    }

    // Create mutex for thread-safe access
    _gif_mutex = xSemaphoreCreateMutex();
    if (!_gif_mutex) {
        ESP_LOGE(TAG, "Failed to create GIF mutex");
        return false;
    }

    // Create GIF playback task via TaskManager on core 1
    _task_running = true;
    bool created = TaskManager::instance().createTask(
        "gif_task",
        "GifPlayer",
        gifTaskImpl,
        this,
        2,                      // Priority (lower than audio output)
        1,                      // Core 1
        8192,                   // 8KB stack
        CleanupPattern::SELF_DELETING,
        "Animated GIF frame rendering"
    );
    if (!created) {
        ESP_LOGE(TAG, "Failed to create GIF task");
        vSemaphoreDelete(_gif_mutex);
        _gif_mutex = nullptr;
        return false;
    }

    _initialized = true;
    ESP_LOGD(TAG, "GIF player initialized successfully (task on core 1)");
    return true;
}

bool GifPlayer::loadGIF(const char* filename) {
    // First, stop any previously playing GIF and close its file handle.
    // This makes the function robust and prevents file handle leaks.
    stop();

    ESP_LOGD(TAG, "📂 Loading GIF: %s", filename);

    // Safety: check existence first
    if (!LittleFS.exists(filename)) {
        ESP_LOGE(TAG, "❌ GIF file does not exist: %s", filename);
        _is_playing = false;
        return false;
    }

    // Pass nullptr for the draw callback to enable full-frame decoding into the buffer.
    if (!_gif.open(filename, openFile, closeFile, readFile, seekFile, nullptr)) {
        ESP_LOGE(TAG, "❌ Failed to open GIF: %s (invalid format or open error)", filename);
        _is_playing = false;
        return false;
    }

    // Calculate centering offsets
    _context.offsetX = (GIF_WIDTH - _gif.getCanvasWidth()) / 2;
    _context.offsetY = (GIF_HEIGHT - _gif.getCanvasHeight()) / 2;

    // Set draw type to COOKED. With a null callback, the library will decode the
    // full frame into the provided buffer.
    _gif.setDrawType(GIF_DRAW_COOKED);
    _gif.setFrameBuf(_context.sharedFrameBuffer);

    ESP_LOGD(TAG, "✅ GIF loaded successfully: %dx%d (offset: %d,%d)",
             _gif.getCanvasWidth(), _gif.getCanvasHeight(),
             _context.offsetX, _context.offsetY);

    return true;
}

bool GifPlayer::playGIF(const char* filename) {
    if (!loadGIF(filename)) {
        return false;
    }
    
    _next_frame_time_ms = millis();
    _is_playing = true;
    _finished_once = false;
    
    return true;
}

bool GifPlayer::takeFinishedOnce() {
    if (_finished_once) {
        _finished_once = false;
        return true;
    }
    return false;
}

bool GifPlayer::drawCurrentFrame() {
    if (!_display) {
        return false;
    }

    bool mutex_taken = false;
    if (_display_mutex) {
        if (xSemaphoreTake(_display_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            mutex_taken = true;
        } else {
            ESP_LOGW(TAG, "Skipping frame draw: failed to take display mutex");
            return false;
        }
    }

    uint16_t* pPixelsBase = (uint16_t*)&_context.sharedFrameBuffer[GIF_WIDTH * GIF_HEIGHT];

    int iX = _gif.getFrameXOff();
    int iY = _gif.getFrameYOff();
    int fW = _gif.getFrameWidth();
    int fH = _gif.getFrameHeight();

    uint16_t* pPixels = pPixelsBase + iX + (iY * GIF_WIDTH);

    for (int y = 0; y < fH; y++) {
        int screen_y = iY + y;
        int line_start = iX;
        int line_end = iX + fW;
        int skip_starts[2];
        int skip_ends[2];
        int skip_count = 0;

        if (screen_y >= UI_ACTIVITY_Y && screen_y < (UI_ACTIVITY_Y + UI_ACTIVITY_HEIGHT)) {
            int skip_start = max(line_start, UI_ACTIVITY_X);
            int skip_end = min(line_end, UI_ACTIVITY_X + UI_ACTIVITY_WIDTH);
            if (skip_start < skip_end) {
                skip_starts[skip_count] = skip_start;
                skip_ends[skip_count] = skip_end;
                skip_count++;
            }
        }

        if (screen_y >= UI_STATUS_Y && screen_y < (UI_STATUS_Y + UI_STATUS_HEIGHT)) {
            int skip_start = max(line_start, UI_STATUS_X);
            int skip_end = min(line_end, UI_STATUS_X + UI_STATUS_WIDTH);
            if (skip_start < skip_end) {
                skip_starts[skip_count] = skip_start;
                skip_ends[skip_count] = skip_end;
                skip_count++;
            }
        }

        if (skip_count == 2 && skip_starts[1] < skip_starts[0]) {
            int tmp_start = skip_starts[0];
            int tmp_end = skip_ends[0];
            skip_starts[0] = skip_starts[1];
            skip_ends[0] = skip_ends[1];
            skip_starts[1] = tmp_start;
            skip_ends[1] = tmp_end;
        }

        auto write_segment = [&](int seg_start, int seg_end) {
            int seg_width = seg_end - seg_start;
            if (seg_width <= 0) {
                return;
            }
            uint16_t* seg_pixels = pPixels + (seg_start - iX);
            uint16_t* out_pixels = seg_pixels;
            if (_effects_manager || (_use_dma && _dmaBuffer)) {
                if (_dmaBuffer) {
                    memcpy(_dmaBuffer, seg_pixels, seg_width * 2);
                    out_pixels = _dmaBuffer;
                }
                if (_effects_manager) {
                    _effects_manager->applyToScanline(out_pixels, seg_width, screen_y, seg_start);
                }
            }
            _display->getAdafruitDisplay()->startWrite();
            _display->getAdafruitDisplay()->setAddrWindow(
                _context.offsetX + seg_start,
                _context.offsetY + iY + y,
                seg_width, 1
            );
            if (_use_dma && _dmaBuffer) {
                _display->getAdafruitDisplay()->writePixels(out_pixels, seg_width);
            } else {
                _display->getAdafruitDisplay()->writePixels(out_pixels, seg_width);
            }
            _display->getAdafruitDisplay()->endWrite();
        };

        if (skip_count == 0) {
            write_segment(line_start, line_end);
        } else {
            int draw_start = line_start;
            for (int i = 0; i < skip_count; i++) {
                if (draw_start < skip_starts[i]) {
                    write_segment(draw_start, skip_starts[i]);
                }
                draw_start = max(draw_start, skip_ends[i]);
            }
            if (draw_start < line_end) {
                write_segment(draw_start, line_end);
            }
        }

        pPixels += GIF_WIDTH;

        if ((y & 3) == 0) {
            taskYIELD();
        }
    }

    if (mutex_taken) {
        xSemaphoreGive(_display_mutex);
    }

    return true;
}

bool GifPlayer::update() {
    if (!_is_playing || !_initialized) {
        return false;
    }

    unsigned long now = millis();
    if ((long)(now - _next_frame_time_ms) < 0) {
        return false;
    }

    int delay_ms = 0;
    bool gif_mutex_taken = false;
    if (_gif_mutex) {
        if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            gif_mutex_taken = true;
        } else {
            ESP_LOGW(TAG, "Skipping frame update: failed to take GIF mutex");
            return false;
        }
    }

    int result = playFrame(false, &delay_ms);
    ESP_LOGD(TAG, "Frame advance result=%d delay=%d playing=%d loop=%d",
             result,
             delay_ms,
             _is_playing ? 1 : 0,
             _should_loop ? 1 : 0);
    if (result == 0) {
        _finished_once = true;
        if (!_should_loop) {
            ESP_LOGD(TAG, "GIF finished (no loop)");
            _is_playing = false;
            stop();
            if (gif_mutex_taken) {
                xSemaphoreGive(_gif_mutex);
            }
            return false;
        }
    } else if (result < 0) {
        int gif_error = _gif.getLastError();
        size_t file_size = _gifFile ? _gifFile.size() : 0;
        size_t file_pos = _gifFile ? _gifFile.position() : 0;
        ESP_LOGE(TAG, "Error playing frame: %d (gif_err=%d:%s pos=%u size=%u)",
                 result,
                 gif_error,
                 gifErrorToString(gif_error),
                 static_cast<unsigned>(file_pos),
                 static_cast<unsigned>(file_size));
        if (gif_error == GIF_EARLY_EOF || file_pos == static_cast<size_t>(-1)) {
            ESP_LOGW(TAG, "Resetting GIF decoder after error (gif_err=%d:%s pos=%u)",
                     gif_error,
                     gifErrorToString(gif_error),
                     static_cast<unsigned>(file_pos));
            _gif.reset();
            int reset_delay_ms = 0;
            int reset_result = playFrame(false, &reset_delay_ms);
            if (reset_result >= 0) {
                drawCurrentFrame();
                if (reset_delay_ms < 10) {
                    reset_delay_ms = 10;
                }
                _next_frame_time_ms = now + reset_delay_ms;
                _is_playing = true;
                _finished_once = false;
                if (gif_mutex_taken) {
                    xSemaphoreGive(_gif_mutex);
                }
                return true;
            }
            ESP_LOGW(TAG, "Reset decode failed: %d (gif_err=%d:%s)",
                     reset_result,
                     _gif.getLastError(),
                     gifErrorToString(_gif.getLastError()));
        }
        _is_playing = false;
        stop();
        if (_current_gif.length() > 0) {
            unsigned long now = millis();
            if (now - _last_error_restart_ms > 500) {
                ESP_LOGW(TAG, "Restarting GIF after error: %s", _current_gif.c_str());
                if (loadGIF(_current_gif.c_str())) {
                    int first_delay_ms = 0;
                    int first_result = playFrame(false, &first_delay_ms);
                    if (first_result >= 0) {
                        drawCurrentFrame();
                        if (first_delay_ms < 10) {
                            first_delay_ms = 10;
                        }
                        _next_frame_time_ms = now + first_delay_ms;
                        _is_playing = true;
                        _finished_once = false;
                        _last_error_restart_ms = now;
                    } else {
                        ESP_LOGW(TAG, "Error decoding GIF first frame after restart: %d (gif_err=%d:%s)",
                                 first_result,
                                 _gif.getLastError(),
                                 gifErrorToString(_gif.getLastError()));
                    }
                }
            }
        }
        if (gif_mutex_taken) {
            xSemaphoreGive(_gif_mutex);
        }
        return false;
    }

    bool drawn = drawCurrentFrame();

    if (gif_mutex_taken) {
        xSemaphoreGive(_gif_mutex);
    }

    if (delay_ms < 10) {
        delay_ms = 10;
    }
    _next_frame_time_ms = now + delay_ms;

    return drawn;
}

bool GifPlayer::renderFirstFrame() {
    if (!_initialized || !_gif_mutex) {
        return false;
    }

    if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Skipping first frame render: failed to take GIF mutex");
        return false;
    }

    if (_current_gif.length() == 0) {
        xSemaphoreGive(_gif_mutex);
        return false;
    }

    _is_playing = false;
    _finished_once = false;

    bool rendered = false;
    if (loadGIF(_current_gif.c_str())) {
        int delay_ms = 0;
        int result = playFrame(false, &delay_ms);
        if (result >= 0) {
            rendered = drawCurrentFrame();
        } else {
            int gif_error = _gif.getLastError();
            ESP_LOGW(TAG, "Error decoding GIF first frame: %d (gif_err=%d:%s)",
                     result,
                     gif_error,
                     gifErrorToString(gif_error));
        }
    }

    xSemaphoreGive(_gif_mutex);
    return rendered;
}

void GifPlayer::resumePlayback() {
    if (!_initialized || !_gif_mutex) {
        return;
    }

    if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (_current_gif.length() == 0) {
        xSemaphoreGive(_gif_mutex);
        return;
    }

    _is_playing = true;
    _next_frame_time_ms = millis();
    ESP_LOGI(TAG, "Resumed GIF playback (current=%s)", _current_gif.c_str());
    xSemaphoreGive(_gif_mutex);
}

int GifPlayer::playFrame(bool bSync, int* delayMilliseconds) {
    return _gif.playFrame(bSync, delayMilliseconds);
}

void GifPlayer::stop() {
    // Close the GIF (this calls closeFile callback)
    _gif.close();

    // Explicitly close the static file handle if it's still open.
    // This is a safety net to ensure no file handle leaks.
    if (_gifFile) {
        _gifFile.close();
    }
    // Note: We do NOT free the sharedFrameBuffer here. It is reused for performance.
}

// ============================================================================
// Static callback functions for AnimatedGIF library
// ============================================================================

void* GifPlayer::openFile(const char* fname, int32_t* pSize) {
    _gifFile = LittleFS.open(fname, "r");
    if (_gifFile) {
        *pSize = _gifFile.size();
        ESP_LOGD(TAG, "Opened file: %s (size: %d)", fname, *pSize);
        return (void*)&_gifFile;
    }
    ESP_LOGE(TAG, "Failed to open file: %s", fname);
    return nullptr;
}

void GifPlayer::closeFile(void* pHandle) {
    File* f = static_cast<File*>(pHandle);
    if (f != nullptr) {
        f->close();
        ESP_LOGD(TAG, "Closed file");
    }
}

int32_t GifPlayer::readFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = static_cast<File*>(pFile->fHandle);
    int32_t bytesToRead = min(iLen, pFile->iSize - pFile->iPos);

    if (bytesToRead <= 0) {
        return 0;
    }

    int32_t bytesRead = f->read(pBuf, bytesToRead);
    pFile->iPos = f->position();
    return bytesRead;
}

int32_t GifPlayer::seekFile(GIFFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

void GifPlayer::drawCallback(GIFDRAW* pDraw) {
    // This callback is no longer used when using the full-canvas COOKED mode,
    // as a nullptr is passed to _gif.open(). The drawing logic is now handled
    // directly within the playGIF method after each frame is decoded.
}

// ============================================================================
// Task-based GIF playback (runs on core 1)
// ============================================================================

void GifPlayer::requestGIF(const char* filename, bool loop) {
    if (!_initialized || !_gif_mutex) {
        return;
    }

    // Thread-safe update of requested GIF
    if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        const bool same_gif = filename && _current_gif == filename;
        const bool restart_same = same_gif && !_is_playing;
        ESP_LOGI(TAG, "Request GIF: %s (loop=%d current=%s playing=%d restart=%d)",
                 filename ? filename : "(null)",
                 loop ? 1 : 0,
                 _current_gif.c_str(),
                 _is_playing ? 1 : 0,
                 restart_same ? 1 : 0);
        if (restart_same) {
            _current_gif = "";
        }
        _requested_gif = String(filename);
        _requested_loop = loop;
        // Immediate acknowledgement to prevent repeated requests
        _finished_once = false; 
        xSemaphoreGive(_gif_mutex);
    }
}

void GifPlayer::stopPlayback() {
    if (!_initialized || !_gif_mutex) {
        return;
    }

    if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _requested_gif = "";
        _current_gif = "";
        _should_loop = false;
        _requested_loop = false;
        _finished_once = false;
        _is_playing = false;
        stop();
        xSemaphoreGive(_gif_mutex);
    }
}

void GifPlayer::gifTaskImpl(void* parameter) {
    GifPlayer* player = static_cast<GifPlayer*>(parameter);
    if (player) {
        player->gifTask();
    }
    TaskManager::instance().markTaskStopped("gif_task");
    vTaskDelete(nullptr);
}

void GifPlayer::gifTask() {
    ESP_LOGD(TAG, "GIF task started on core %d", xPortGetCoreID());

    static uint32_t last_restart_ms = 0;

    while (_task_running) {
        // Check if there's a new GIF requested
        if (xSemaphoreTake(_gif_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (_requested_gif.length() > 0 && _requested_gif != _current_gif) {
                // New GIF requested
                ESP_LOGI(TAG, "Switching GIF: %s -> %s (was_playing=%d)",
                         _current_gif.c_str(),
                         _requested_gif.c_str(),
                         _is_playing ? 1 : 0);
                _current_gif = _requested_gif;
                _should_loop = _requested_loop;
                _finished_once = false;
                
                ESP_LOGD(TAG, "Playing GIF on core 1: %s", _current_gif.c_str());
                playGIF(_current_gif.c_str());
                
            } else if (_current_gif.length() > 0 && !_is_playing && _should_loop) {
                uint32_t now = millis();
                if (now - last_restart_ms > 500) {
                    ESP_LOGW(TAG, "Restarting GIF playback after stop: %s", _current_gif.c_str());
                    playGIF(_current_gif.c_str());
                    last_restart_ms = now;
                }
            }
            xSemaphoreGive(_gif_mutex);
        }

        if (_is_playing) {
            update();
            // Yield a bit to not starve other tasks on this core (if any), 
            // but update() handles frame timing so we want to be responsive.
            vTaskDelay(1); 
        } else {
            // No GIF playing, sleep longer
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    ESP_LOGD(TAG, "GIF task stopped");
}
