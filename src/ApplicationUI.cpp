/**
 * ApplicationUI.cpp
 *
 * Implementation for ApplicationUI.
 */

#include "ApplicationUI.h"
#include "ArduinoSSD1351.h"
#include "AudioVisualizer.h"
#include "Axp2101.h"
#include "GifManager.h"
#include "GifPlayer.h"
#include "EffectsManager.h"
#include "DigitalClock.h"
#include "SystemState.h"
#include "DosBootAnimator.h"
#include "DeviceConfig.h"
#include "DeviceSimulator.h"
#include "AdxlManager.h"
#include "HapticsVisualizer.h"
#include "StartupImage.h"
#include "TaskManager.h"
#include "UIVisualizer.h"
#include "Mp3Player.h"
#include "HapticsManager.h"
#include "RetroTints.h"
#include "WebsocketClient.h"
#include "WifiManager.h"
#include <esp_log.h>
#include <stdio.h>

static const char* TAG = "ApplicationUI";
static constexpr unsigned long TIMER_FLASH_INTERVAL_MS = 200;
static constexpr uint32_t SPEAKING_GIF_ACTIVITY_MS = 200;

ApplicationUI::ApplicationUI(LittleFSManager* fs, ArduinoSSD1351* display)
    : _display(display)
    , _fs(fs)
    , _audio_visualizer(nullptr)
    , _mp3_player(nullptr)
    , _haptics_manager(nullptr)
    , _timer_manager(nullptr)
    , _adxl_manager(nullptr)
    , _effects_manager(nullptr)
    , _ui_visualizer(nullptr)
    , _gif_manager(nullptr)
    , _digital_clock(nullptr)
    , _has_gifs(false)
    , _sleep_animator(nullptr)
    , _motion_animator(nullptr)
    , _charging_animator(nullptr)
    , _display_mutex(nullptr)
    , _showing_activation(false)
    , _showing_clock(false)
    , _activation_url("")
    , _activation_code("")
    , _activation_dirty(false)
    , _show_center_visualization(true)
    , _animation_phase(0)
    , _current_bar_state(SYSTEM_STATE_STARTING)
    , _current_gif_state(SYSTEM_STATE_STARTING)
    , _ws_connecting(false)
    , _cached_ws_hello(false)
    , _has_cached_audio(false)
    , _status_bar_dirty(true)
    , _animate_active(false)
    , _timer_flash_active(false)
    , _timer_flash_visible(false)
    , _timer_flash_remaining(0)
    , _timer_flash_next_ms(0)
    , _timer_flash_continuous(false)
    , _dos_boot(nullptr)
    , _haptics_visualizer(nullptr)
{
    _ui_visualizer = new UIVisualizer(_display);
    _haptics_visualizer = new HapticsVisualizer();
    _dos_boot = new DosBootAnimator();
    _digital_clock = new DigitalClock(_display);
    _sleep_animator = new SleepAnimator();
    _motion_animator = new MotionAnimator();
    _charging_animator = new ChargingAnimator();
    
    // Create mutex for display protection
    _display_mutex = xSemaphoreCreateMutex();
    
    // Initialize status cache
    _status_cache.wifi_color = COLOR_WHITE;
    _status_cache.battery_color = COLOR_WHITE;
    _status_cache.battery_fill_width = 0;
    _status_cache.battery_connected = false;
    _status_cache.battery_percentage = 0;
    _status_cache.timer_visible = false;
    _status_cache.timer_text = "";
    _status_cache.timer_remaining_seconds = 0;
    _status_cache.timer_display_format = TimerManager::DisplayFormat::None;

    // Initialize GifManager only if GIFs exist
    _has_gifs = GifManager::hasGifs(_fs);
    if (_has_gifs) {
        _gif_manager = new GifManager();
    } else {
        ESP_LOGW(TAG, "No GIFs found, GifManager disabled");
        _gif_manager = nullptr;
    }

    if (_sleep_animator) {
        _sleep_animator->setGifManager(_gif_manager);
    }
    if (_motion_animator) {
        _motion_animator->setGifManager(_gif_manager);
        _motion_animator->setAdxlManager(_adxl_manager);
        _motion_animator->setMp3Player(_mp3_player);
        _motion_animator->setHapticsManager(_haptics_manager);
    }
    if (_charging_animator) {
        _charging_animator->setGifManager(_gif_manager);
        _charging_animator->setMp3Player(_mp3_player);
    }
}

ApplicationUI::~ApplicationUI()
{
    // Stop animation task via TaskManager
    _animate_active = false;
    TaskManager::instance().stopTask("ui_anim");

    if (_gif_manager) {
        delete _gif_manager;
        _gif_manager = nullptr;
    }

    if (_sleep_animator) {
        delete _sleep_animator;
        _sleep_animator = nullptr;
    }
    if (_motion_animator) {
        delete _motion_animator;
        _motion_animator = nullptr;
    }
    if (_charging_animator) {
        delete _charging_animator;
        _charging_animator = nullptr;
    }

    if (_haptics_visualizer) {
        _haptics_visualizer->stop();
        delete _haptics_visualizer;
        _haptics_visualizer = nullptr;
    }

    if (_digital_clock) {
        delete _digital_clock;
        _digital_clock = nullptr;
    }
    
    if (_ui_visualizer) {
        delete _ui_visualizer;
        _ui_visualizer = nullptr;
    }

    if (_display_mutex) {
        vSemaphoreDelete((SemaphoreHandle_t)_display_mutex);
        _display_mutex = nullptr;
    }

    if (_dos_boot) {
        delete _dos_boot;
        _dos_boot = nullptr;
    }
}

void ApplicationUI::begin()
{
    ESP_LOGI(TAG, "Initializing ApplicationUI");

    // Create animation task via TaskManager
    bool created = TaskManager::instance().createTask(
        "ui_anim",
        "ApplicationUI",
        animationTask,
        this,
        1,                      // Priority (low, background)
        1,                      // Core 1
        4096,                   // 4KB stack
        CleanupPattern::FORCE_DELETE,
        "UI animation and status bar updates"
    );
    if (!created) {
        ESP_LOGE(TAG, "Failed to create animation task");
    }

    bool dos_started = false;
    if (_dos_boot) {
        _dos_boot->begin(_display, (SemaphoreHandle_t)_display_mutex);
#if USE_DOS_BOOT_ANIMATION
        if (_effects_manager && _effects_manager->isTintEnabled()) {
            RetroEffects::TintParams tint_params = _effects_manager->getTintParams();
            _dos_boot->setTintColor(tint_params.tint_color, true);
        } else {
            _dos_boot->setTintColor(COLOR_YELLOW, false);
        }
#endif
#if USE_DOS_BOOT_ANIMATION
        _dos_boot->runFast();
        dos_started = true;
#endif
    }

    if (dos_started && _display) {
        if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool tint_enabled = _effects_manager && _effects_manager->isTintEnabled();
            if (tint_enabled) {
                RetroEffects::TintParams tint_params = _effects_manager->getTintParams();
                uint16_t line_buffer[DISPLAY_WIDTH];
                Adafruit_SSD1351* gfx = _display->getAdafruitDisplay();
                gfx->startWrite();
                for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                    const uint16_t* src_line = STARTUP_STATIC + (y * DISPLAY_WIDTH);
                    for (int x = 0; x < DISPLAY_WIDTH; x++) {
                        line_buffer[x] = RetroTints::applyTintPixel(
                            src_line[x],
                            tint_params,
                            x,
                            y
                        );
                    }
                    gfx->setAddrWindow(0, y, DISPLAY_WIDTH, 1);
                    gfx->writePixels(line_buffer, DISPLAY_WIDTH);
                }
                gfx->endWrite();
            } else {
                _display->getAdafruitDisplay()->drawRGBBitmap(
                    0,
                    0,
                    STARTUP_STATIC,
                    DISPLAY_WIDTH,
                    DISPLAY_HEIGHT
                );
            }
            xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
        }
        delay(600);
    }

    if (_gif_manager) {
        // Pass filesystem, display and mutex to manager for player initialization
        _gif_manager->begin(_fs, _display, (SemaphoreHandle_t)_display_mutex);

        if (_effects_manager && _gif_manager->getPlayer()) {
            _gif_manager->getPlayer()->setEffectsManager(_effects_manager);
        }

        if (_sleep_animator) {
            _sleep_animator->setGifManager(_gif_manager);
        }
        if (_motion_animator) {
            _motion_animator->setGifManager(_gif_manager);
        }
        if (_charging_animator) {
            _charging_animator->setGifManager(_gif_manager);
        }
        
        // Start initial state GIF (no loop, play once then transition)
        _gif_manager->playState("startup", false);
    }

    // Temporarily disable haptic visualizer sync (task not started).

    ESP_LOGI(TAG, "ApplicationUI initialized");
}

void ApplicationUI::setToneGenerator(ToneGenerator* tone) {
    if (_dos_boot) {
        _dos_boot->setToneGenerator(tone);
    }
}

void ApplicationUI::setTimerManager(TimerManager* timer_manager) {
    _timer_manager = timer_manager;
    _status_bar_dirty = true;
}

void ApplicationUI::setAdxlManager(AdxlManager* adxl_manager) {
    _adxl_manager = adxl_manager;
    if (_motion_animator) {
        _motion_animator->setAdxlManager(adxl_manager);
    }
}

void ApplicationUI::setEffectsManager(EffectsManager* effects_manager) {
    _effects_manager = effects_manager;
    if (_gif_manager && _gif_manager->getPlayer()) {
        _gif_manager->getPlayer()->setEffectsManager(_effects_manager);
    }
}

void ApplicationUI::setTimerFlashActive(bool active) {
    if (active && _timer_flash_active && _timer_flash_continuous) {
        return;
    }
    if (!active && !_timer_flash_active && !_timer_flash_visible) {
        _timer_flash_continuous = false;
        return;
    }
    if (active) {
        _timer_flash_active = true;
        _timer_flash_continuous = true;
        _timer_flash_visible = true;
        _timer_flash_remaining = 0;
        _timer_flash_next_ms = millis() + TIMER_FLASH_INTERVAL_MS;
    } else if (_timer_flash_active || _timer_flash_visible) {
        _timer_flash_active = false;
        _timer_flash_continuous = false;
        _timer_flash_visible = false;
        _timer_flash_remaining = 0;
    } else {
        _timer_flash_continuous = false;
    }
    _status_bar_dirty = true;
}

void ApplicationUI::startTimerFlash(uint8_t flashes) {
    if (flashes == 0) {
        return;
    }
    _timer_flash_active = true;
    _timer_flash_visible = true;
    _timer_flash_continuous = false;
    _timer_flash_remaining = flashes;
    _timer_flash_remaining--;
    _timer_flash_next_ms = millis() + TIMER_FLASH_INTERVAL_MS;
    _status_bar_dirty = true;
}

void ApplicationUI::animationTask(void* arg)
{
    ApplicationUI* ui = static_cast<ApplicationUI*>(arg);
    if (!ui) {
        TaskManager::instance().markTaskStopped("ui_anim");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        if (ui->_animate_active) {
            ui->updateAnimation();
        }
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_INTERVAL_MS));
    }
}

void ApplicationUI::updateAnimation()
{
    _animation_phase++;

    if (_ui_visualizer) {
        _ui_visualizer->updateAnimation((SystemState)_current_bar_state, _cached_ws_hello);
    }
}


void ApplicationUI::updateTimerFlash()
{
    if (!_timer_flash_active) {
        return;
    }

    unsigned long now = millis();
    if (now < _timer_flash_next_ms) {
        return;
    }

    _timer_flash_next_ms = now + TIMER_FLASH_INTERVAL_MS;
    if (_timer_flash_continuous) {
        _timer_flash_visible = !_timer_flash_visible;
        _status_bar_dirty = true;
        return;
    }
    if (_timer_flash_visible) {
        _timer_flash_visible = false;
        if (_timer_flash_remaining == 0) {
            _timer_flash_active = false;
        }
    } else {
        if (_timer_flash_remaining == 0) {
            _timer_flash_active = false;
            return;
        }
        _timer_flash_visible = true;
        _timer_flash_remaining--;
    }
    _status_bar_dirty = true;
}

void ApplicationUI::formatTimerText(uint32_t remaining_seconds,
                                    TimerManager::DisplayFormat format,
                                    char* buffer,
                                    size_t buffer_size) const
{
    if (!buffer || buffer_size == 0) {
        return;
    }

    uint32_t hours = 0;
    uint32_t minutes = 0;
    uint32_t seconds = 0;

    switch (format) {
        case TimerManager::DisplayFormat::Hours:
            hours = remaining_seconds / 3600U;
            minutes = (remaining_seconds % 3600U) / 60U;
            seconds = remaining_seconds % 60U;
            snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu",
                     static_cast<unsigned long>(hours),
                     static_cast<unsigned long>(minutes),
                     static_cast<unsigned long>(seconds));
            break;
        case TimerManager::DisplayFormat::Minutes:
            minutes = remaining_seconds / 60U;
            seconds = remaining_seconds % 60U;
            snprintf(buffer, buffer_size, "%02lu:%02lu",
                     static_cast<unsigned long>(minutes),
                     static_cast<unsigned long>(seconds));
            break;
        case TimerManager::DisplayFormat::Seconds:
            seconds = remaining_seconds;
            snprintf(buffer, buffer_size, "00:%02lu",
                     static_cast<unsigned long>(seconds));
            break;
        case TimerManager::DisplayFormat::None:
        default:
            buffer[0] = '\0';
            break;
    }
}

String ApplicationUI::mapSystemStateToName(SystemState state)
{
    switch (state) {
        case SYSTEM_STATE_IDLE:      return "idle";
        case SYSTEM_STATE_LISTENING: return "listening";
        case SYSTEM_STATE_SPEAKING:  return "speaking";
        case SYSTEM_STATE_LOADING:   return "connecting";
        case SYSTEM_STATE_STARTING:  return "startup";
        case SYSTEM_STATE_CONNECTING:return "connecting";
        case SYSTEM_STATE_WIFI_CONFIGURING: return "wifi_config";
        case SYSTEM_STATE_ACTIVATING: return "activating";
        default:                     return "idle";
    }
}

void ApplicationUI::update(
    SystemStateManager* state_manager,
    WifiManager* wifi_client,
    WebSocketClient* ws_client,
    bool ws_hello_received,
    AXP2101* power_manager
)
{
    if (!_display) return;

    if (_showing_clock) {
        SystemState current_state = state_manager->getState();
        SystemState status_state = current_state;
        bool ws_connecting = false;
        if (ws_client && ws_client->isConnected() && !ws_hello_received) {
            ws_connecting = true;
        }
        _ws_connecting = ws_connecting;
        _cached_ws_hello = ws_hello_received;
        _current_bar_state = status_state;
        if (_audio_visualizer) {
            uint32_t now = millis();
            uint32_t last_update = _audio_visualizer->getLastUpdateMs();
            bool viz_active = (last_update > 0) &&
                              (now - last_update <= SPEAKING_GIF_ACTIVITY_MS);
            if (viz_active) {
                _current_gif_state = SYSTEM_STATE_SPEAKING;
            } else if (status_state == SYSTEM_STATE_SPEAKING) {
                _current_gif_state = SYSTEM_STATE_LISTENING;
            } else {
                _current_gif_state = status_state;
            }
        } else {
            _current_gif_state = status_state;
        }
        _animate_active = (status_state == SYSTEM_STATE_LISTENING ||
                           status_state == SYSTEM_STATE_SPEAKING ||
                           status_state == SYSTEM_STATE_CONNECTING ||
                           status_state == SYSTEM_STATE_LOADING ||
                           (status_state == SYSTEM_STATE_IDLE && ws_connecting));

        updateStatusBarCache(wifi_client, power_manager);
        if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_status_bar_dirty) {
                drawStatusBar();
                _status_bar_dirty = false;
            }
            if (_ui_visualizer) {
                _ui_visualizer->createStatusVisualizer(status_state, ws_hello_received, ws_connecting);
            }
            if (_digital_clock) {
                _digital_clock->draw();
            }
            xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
        }
        if (_haptics_visualizer) {
            _haptics_visualizer->setState(status_state);
        }
        return;
    }

    if (_showing_activation) {
        updateStatusBarCache(wifi_client, power_manager);
        if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_activation_dirty) {
                drawActivationScreen(_activation_url, _activation_code);
            }
            if (_status_bar_dirty || _activation_dirty) {
                drawStatusBar();
                _status_bar_dirty = false;
            }
            _activation_dirty = false;
            xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
        }
        if (_haptics_visualizer) {
            _haptics_visualizer->setState(state_manager->getState());
        }
        return;
    }

    SystemState current_state = state_manager->getState();
    SystemState gif_state = current_state;
    if (_audio_visualizer) {
        uint32_t now = millis();
        uint32_t last_update = _audio_visualizer->getLastUpdateMs();
        bool viz_active = (last_update > 0) &&
                          (now - last_update <= SPEAKING_GIF_ACTIVITY_MS);
        if (viz_active) {
            gif_state = SYSTEM_STATE_SPEAKING;
        } else if (gif_state == SYSTEM_STATE_SPEAKING) {
            gif_state = SYSTEM_STATE_LISTENING;
        }
    }
    bool just_transitioned = false;
    bool sleep_animation_active = false;
    bool sleep_resume = false;
    bool motion_animation_active = false;
    bool motion_resume = false;
    bool charging_animation_active = false;

    if (_sleep_animator && _adxl_manager) {
        bool sleeping = _adxl_manager->checkMotionState(MotionState::LIGHT_SLEEP);
        _sleep_animator->update(sleeping);
        sleep_animation_active = _sleep_animator->isActive();
        sleep_resume = _sleep_animator->needsResume();
        if (sleep_resume) {
            _sleep_animator->consumeResumeRequest();
        }
    }

    if (_charging_animator && !sleep_animation_active) {
        charging_animation_active = _charging_animator->update(current_state, power_manager);
    }

    if (!charging_animation_active &&
        _motion_animator &&
        _adxl_manager &&
        !sleep_animation_active) {
        const bool allow_motion = (current_state != SYSTEM_STATE_STARTING);
        motion_animation_active = _motion_animator->update(current_state, allow_motion);
        motion_resume = _motion_animator->needsResume();
        if (motion_resume) {
            _motion_animator->consumeResumeRequest();
        }
    }

    // Transition from STARTING to IDLE once the animation has finished at least one loop
    if (current_state == SYSTEM_STATE_STARTING && _gif_manager) {
        if (_gif_manager->getPlayer() && _gif_manager->getPlayer()->hasFinishedOnce()) {
            ESP_LOGD(TAG, "Starting animation finished, transitioning to IDLE");
            state_manager->setState(SYSTEM_STATE_IDLE);
            just_transitioned = true;
            current_state = SYSTEM_STATE_IDLE;
        }
    }

    // Update GIF manager to handle random variation switching
    // Skip this if we just transitioned out of STARTING to prevent re-playing startup GIF
    if (_gif_manager && !just_transitioned && !sleep_animation_active &&
        !motion_animation_active && !charging_animation_active) {
        _gif_manager->update();
    }

    // Check for state change to update GIF
    if (_current_gif_state != gif_state) {
        ESP_LOGD(TAG, "GIF state changed: %d -> %d", _current_gif_state, gif_state);
        
        if (_gif_manager && !sleep_animation_active && !motion_animation_active &&
            !charging_animation_active) {
            String state_name = mapSystemStateToName(gif_state);
            // Let manager handle playback logic
            _gif_manager->playState(state_name);
        }
    }

    _current_gif_state = gif_state;
    _current_bar_state = current_state;

    bool ws_connecting = false;
    if (ws_client && ws_client->isConnected() && !ws_hello_received) {
        ws_connecting = true;
    }
    _ws_connecting = ws_connecting;
    _cached_ws_hello = ws_hello_received;

    bool should_animate = (current_state == SYSTEM_STATE_LISTENING ||
                           current_state == SYSTEM_STATE_SPEAKING ||
                           current_state == SYSTEM_STATE_CONNECTING ||
                           current_state == SYSTEM_STATE_LOADING ||
                           (current_state == SYSTEM_STATE_IDLE && ws_connecting));

    _animate_active = should_animate;

    updateStatusBarCache(wifi_client, power_manager);

    // Protect display access with mutex
    if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Draw directly; GIF rendering skips status bar regions.
        if (_status_bar_dirty) {
            drawStatusBar();
            _status_bar_dirty = false;
        }

        if (_ui_visualizer) {
            _ui_visualizer->createStatusVisualizer(current_state, ws_hello_received, _ws_connecting);
        }

        if (!_has_gifs && _ui_visualizer) {
            _ui_visualizer->createVisualizer(current_state, ws_hello_received, _ws_connecting);
        }
        
        xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
    }

    if ((sleep_resume || motion_resume) && _gif_manager && !charging_animation_active) {
        String state_name = mapSystemStateToName(gif_state);
        _gif_manager->playState(state_name);
    }

    if (_haptics_visualizer) {
        _haptics_visualizer->setState(current_state);
    }
}

void ApplicationUI::setAudioVisualizer(AudioVisualizer* visualizer)
{
    _audio_visualizer = visualizer;
    if (_ui_visualizer) {
        _ui_visualizer->setAudioVisualizer(visualizer);
    }
    // Haptic visualizer sync temporarily disabled.
}

void ApplicationUI::setMp3Player(Mp3Player* player)
{
    _mp3_player = player;
    if (_motion_animator) {
        _motion_animator->setMp3Player(player);
    }
    if (_charging_animator) {
        _charging_animator->setMp3Player(player);
    }
}

void ApplicationUI::setHapticsManager(HapticsManager* manager)
{
    _haptics_manager = manager;
    if (_motion_animator) {
        _motion_animator->setHapticsManager(manager);
    }
    if (_haptics_visualizer) {
        _haptics_visualizer->setHapticsManager(manager);
    }
}
void ApplicationUI::showActivation(const String& url, const String& code)
{
    if (_showing_activation && _activation_url == url && _activation_code == code) {
        return;
    }

    if (!_showing_activation && _gif_manager) {
        GifPlayer* player = _gif_manager->getPlayer();
        if (player) {
            player->stopPlayback();
        }
    }

    _showing_activation = true;
    _activation_url = url;
    _activation_code = code;
    _activation_dirty = true;
    _animate_active = false;
    ESP_LOGI(TAG, "Showing activation: %s / %s", url.c_str(), code.c_str());
}

void ApplicationUI::clearActivation()
{
    _showing_activation = false;
    _activation_url = "";
    _activation_code = "";
    _activation_dirty = false;
    _status_bar_dirty = true;
    if (_display) {
        if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _display->getAdafruitDisplay()->fillScreen(COLOR_BLACK);
            xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
        }
    }

    if (_gif_manager) {
        _gif_manager->playState("idle");
    }

    ESP_LOGI(TAG, "Activation cleared");
}

void ApplicationUI::playEmotionOnce(const String& emotion)
{
    if (_showing_activation || _showing_clock || !_gif_manager) {
        return;
    }
    _gif_manager->playEmotionOnce(emotion);
}

bool ApplicationUI::showClock(const String& timezone_name)
{
    if (!_display || !_digital_clock) {
        return false;
    }

    if (_showing_activation) {
        return false;
    }

    if (_gif_manager) {
        GifPlayer* player = _gif_manager->getPlayer();
        if (player) {
            player->stopPlayback();
        }
    }

    _showing_clock = true;
    _status_bar_dirty = true;
    _animate_active = false;
    _digital_clock->reset();
    if (xSemaphoreTake((SemaphoreHandle_t)_display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        int clear_width = BATTERY_X - 2;
        if (clear_width > 0) {
            _display->getAdafruitDisplay()->fillRect(0, 0, clear_width, WIFI_CIRCLE_SIZE + 4, COLOR_BLACK);
        }
        xSemaphoreGive((SemaphoreHandle_t)_display_mutex);
    }
    ESP_LOGI(TAG, "Clock display enabled: %s", timezone_name.c_str());
    return true;
}

void ApplicationUI::clearClock()
{
    if (!_showing_clock) {
        return;
    }

    _showing_clock = false;
    _status_bar_dirty = true;

    SystemState resume_state = (SystemState)_current_gif_state;
    _current_gif_state = SYSTEM_STATE_UNKNOWN;

    if (_gif_manager) {
        String state_name = mapSystemStateToName(resume_state);
        _gif_manager->playState(state_name);
        GifPlayer* player = _gif_manager->getPlayer();
        if (player) {
            player->renderFirstFrame();
            player->resumePlayback();
        }
    }

    if (_mp3_player && !_mp3_player->isPlaying()) {
        _mp3_player->playFile("/sounds/interrupt.mp3");
    }
    if (_haptics_manager && _haptics_manager->isReady() && _haptics_manager->isEnabled()) {
        _haptics_manager->playEventHaptic(HapticsManager::HAPTIC_EVENT_INTERRUPT);
    }

    ESP_LOGI(TAG, "Clock display cleared");
}

void ApplicationUI::drawStatusBar()
{
    if (!_display) return;

    auto* gfx = _display->getAdafruitDisplay();
    int timer_x = TIMER_TEXT_X;
    if (timer_x < 0) {
        timer_x = 0;
    }
    int timer_width = BATTERY_X - TIMER_TEXT_GAP - timer_x;
    if (timer_width > 0) {
        gfx->fillRect(timer_x, 0, timer_width, TIMER_TEXT_HEIGHT, COLOR_BLACK);
        if (_status_cache.timer_visible && _status_cache.timer_text.length() > 0) {
            gfx->setTextColor(COLOR_YELLOW);
            gfx->setFont();
            gfx->setTextSize(1);
            gfx->setCursor(timer_x, TIMER_TEXT_Y);
            gfx->print(_status_cache.timer_text);
        }
    }

    gfx->fillCircle(
        WIFI_CIRCLE_X + (WIFI_CIRCLE_SIZE / 2),
        WIFI_CIRCLE_Y + (WIFI_CIRCLE_SIZE / 2),
        WIFI_CIRCLE_SIZE / 2,
        _status_cache.wifi_color
    );

    gfx->drawRoundRect(BATTERY_X, BATTERY_Y, BATTERY_WIDTH, BATTERY_HEIGHT, BATTERY_ROUND_RADIUS, _status_cache.battery_color);

    gfx->fillRect(
        BATTERY_X + BATTERY_FILL_X_OFFSET,
        BATTERY_Y + BATTERY_FILL_Y_OFFSET,
        BATTERY_FILL_WIDTH,
        BATTERY_FILL_HEIGHT,
        COLOR_BLACK
    );

    if (_status_cache.battery_fill_width > 0) {
        gfx->fillRoundRect(BATTERY_X + BATTERY_FILL_X_OFFSET, BATTERY_Y + BATTERY_FILL_Y_OFFSET, _status_cache.battery_fill_width, BATTERY_FILL_HEIGHT, 1, _status_cache.battery_color);
    }
}

void ApplicationUI::updateStatusBarCache(WifiManager* wifi_client, AXP2101* power_manager)
{
    auto previous = _status_cache;

    if (wifi_client && wifi_client->isConnected()) {
        _status_cache.wifi_color = COLOR_GREEN;
    } else {
        _status_cache.wifi_color = COLOR_YELLOW;
    }

    if (power_manager) {
        uint8_t battery_percentage = 0;
        _status_cache.battery_connected = power_manager->getBatteryPercentage(&battery_percentage);
        _status_cache.battery_percentage = battery_percentage;

        if (_status_cache.battery_connected) {
            _status_cache.battery_color = COLOR_GREEN;
            if (battery_percentage <= 20) {
                _status_cache.battery_color = COLOR_RED;
            } else if (battery_percentage <= 60) {
                _status_cache.battery_color = COLOR_YELLOW;
            }
            _status_cache.battery_fill_width = (battery_percentage * BATTERY_FILL_WIDTH) / 100;
        } else {
            _status_cache.battery_color = 0x18E3; // Dimmed white for disconnected
            _status_cache.battery_fill_width = 0;
        }
    }

    updateTimerFlash();

    bool timer_running = false;
    uint32_t timer_remaining = 0;
    TimerManager::DisplayFormat timer_format = TimerManager::DisplayFormat::None;

    if (DeviceSimulator::isTimerSimEnabled()) {
        uint8_t sim_format = 0;
        bool sim_expired = false;
        timer_running = DeviceSimulator::readTimerState(&timer_remaining, &sim_format, &sim_expired);
        switch (sim_format) {
            case 1:
                timer_format = TimerManager::DisplayFormat::Seconds;
                break;
            case 2:
                timer_format = TimerManager::DisplayFormat::Minutes;
                break;
            case 3:
                timer_format = TimerManager::DisplayFormat::Hours;
                break;
            default:
                timer_format = TimerManager::DisplayFormat::None;
                break;
        }
        if (sim_expired) {
            startTimerFlash(3);
        }
        if (timer_running) {
            _timer_flash_active = false;
            _timer_flash_visible = true;
        } else if (_timer_flash_active) {
            if (timer_format == TimerManager::DisplayFormat::None) {
                timer_format = previous.timer_display_format;
            }
        }
    } else if (_timer_manager) {
        timer_running = _timer_manager->isRunning();
        if (timer_running) {
            timer_remaining = _timer_manager->remainingSeconds();
            timer_format = _timer_manager->displayFormat();
            if (timer_format == TimerManager::DisplayFormat::None) {
                timer_format = TimerManager::DisplayFormat::Seconds;
            }
            _timer_flash_active = false;
            _timer_flash_visible = true;
        } else if (_timer_flash_active) {
            timer_format = _timer_manager->lastDisplayFormat();
            if (timer_format == TimerManager::DisplayFormat::None) {
                timer_format = TimerManager::DisplayFormat::Seconds;
            }
        }
    }

    bool timer_visible = timer_running ||
                         (_timer_flash_active && _timer_flash_visible);
    char timer_buffer[16] = {0};
    if (timer_visible) {
        uint32_t display_seconds = timer_running ? timer_remaining : 0;
        formatTimerText(display_seconds, timer_format, timer_buffer, sizeof(timer_buffer));
    }

    _status_cache.timer_visible = timer_visible;
    _status_cache.timer_text = timer_buffer;
    _status_cache.timer_remaining_seconds = timer_remaining;
    _status_cache.timer_display_format = timer_format;

    if (previous.wifi_color != _status_cache.wifi_color ||
        previous.battery_color != _status_cache.battery_color ||
        previous.battery_fill_width != _status_cache.battery_fill_width ||
        previous.timer_visible != _status_cache.timer_visible ||
        previous.timer_text != _status_cache.timer_text ||
        previous.timer_remaining_seconds != _status_cache.timer_remaining_seconds ||
        previous.timer_display_format != _status_cache.timer_display_format) {
        _status_bar_dirty = true;
    }
}

void ApplicationUI::drawActivationScreen(const String& url, const String& code)
{
    if (!_display) return;

    auto* gfx = _display->getAdafruitDisplay();
    gfx->fillScreen(COLOR_BLACK);
    int content_top = STATUS_BAR_Y + WIFI_CIRCLE_SIZE + 32;

    gfx->setTextColor(COLOR_YELLOW);
    gfx->setTextSize(1);
    gfx->setCursor(6, content_top);
    gfx->print("Activation Code");

    int16_t label_x1, label_y1;
    uint16_t label_w, label_h;
    gfx->getTextBounds("Activation Code", 0, 0, &label_x1, &label_y1, &label_w, &label_h);

    int16_t x1, y1;
    uint16_t w, h;
    gfx->setTextSize(3);
    gfx->getTextBounds(code, 0, 0, &x1, &y1, &w, &h);
    int16_t code_x = 6;
    int16_t code_y = content_top + (int)label_h + 8;

    if (code.length() > 0) {
        gfx->setTextColor(COLOR_WHITE);
        gfx->setCursor(code_x, code_y);
        gfx->print(code);
    }

    String display_url = url;
    int newline = display_url.indexOf('\n');
    if (newline >= 0) {
        display_url = display_url.substring(0, newline);
    }

    if (display_url.length() > 0) {
        gfx->setTextColor(COLOR_WHITE);
        gfx->setTextSize(1);
        int16_t url_y = code_y + (int)h + 8;
        gfx->setCursor(6, url_y);
        gfx->print(display_url);
    }
}
