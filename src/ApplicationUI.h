/**
 * ApplicationUI.h
 *
 * Declarations for ApplicationUI.
 */

#pragma once

#include "LittlefsManager.h"
#include "DigitalClockController.h"
#include "SystemState.h"
#include "TimerManager.h"
#include "StatusBarLayout.h"
#include "SleepAnimator.h"
#include "MotionAnimator.h"
#include "ChargingAnimator.h"
#include <Arduino.h>
#include <vector>

// Forward declarations
/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351;
/**
 * @brief AudioVisualizer.
 */
class AudioVisualizer;
/**
 * @brief AXP2101.
 */
class AXP2101;
/**
 * @brief WifiManager.
 */
class WifiManager;
/**
 * @brief WebSocketClient.
 */
class WebSocketClient;
/**
 * @brief SystemStateManager.
 */
class SystemStateManager;
/**
 * @brief UIVisualizer.
 */
class UIVisualizer;
/**
 * @brief GifPlayer.
 */
class GifPlayer;
/**
 * @brief GifManager.
 */
class GifManager;
class DigitalClock;
class Mp3Player;
class HapticsManager;
class ToneGenerator;
class DosBootAnimator;
class AdxlManager;
class HapticsVisualizer;
class EffectsManager;

// Animation timing
#define ANIMATION_INTERVAL_MS 175

// RGB565 color constants
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_RED     0xF800
#define COLOR_ORANGE  0xFD20


/**
 * ApplicationUI - UI subsystem management
 *
 * Features:
 * - Status bar (WiFi, Battery)
 * - Activity dots (Listening/Processing indication)
 * - Center audio visualization (Speaking)
 * - Activation screen
 * - Startup image
 */
class ApplicationUI : public DigitalClockController {
public:
    /**
     * @brief Construct UI subsystem instance
     *
     * @param display Pointer to initialized ArduinoSSD1351 display
     */
    ApplicationUI(LittleFSManager* fs, ArduinoSSD1351* display);

    /**
     * @brief Destroy UI subsystem
     */
    ~ApplicationUI();

    /**
     * @brief Initialize UI subsystem
     */
    void begin();

    /**
     * @brief Update UI state
     *
     * @param state_manager System state manager
     * @param wifi_client Wifi manager for status
     * @param ws_client WebSocket client for status
     * @param ws_hello_received WebSocket hello received flag
     * @param power_manager Power manager for battery status
     */
    void update(
        SystemStateManager* state_manager,
        WifiManager* wifi_client,
        WebSocketClient* ws_client,
        bool ws_hello_received,
        AXP2101* power_manager
    );

    /**
     * @brief Set audio visualizer for real-time visualization
     *
     * @param visualizer Pointer to AudioVisualizer
     */
    void setAudioVisualizer(AudioVisualizer* visualizer);
    void setMp3Player(Mp3Player* player);
    void setHapticsManager(HapticsManager* manager);
    void setToneGenerator(ToneGenerator* tone);
    void setTimerManager(TimerManager* timer_manager);
    void setAdxlManager(AdxlManager* adxl_manager);
    void setEffectsManager(EffectsManager* effects_manager);
    void setTimerFlashActive(bool active);
    void startTimerFlash(uint8_t flashes = 3);

    /**
     * @brief Show activation screen
     *
     * @param url Activation URL
     * @param code Activation code
     */
    void showActivation(const String& url, const String& code);

    /**
     * @brief Clear activation screen
     */
    void clearActivation();

    /**
     * @brief Play a one-shot emotion GIF
     *
     * @param emotion Emotion name (e.g., "happy")
     */
    void playEmotionOnce(const String& emotion);
    bool showClock(const String& timezone_name);
    void clearClock();

    /**
     * @brief Check if activation is currently shown
     */
    bool isShowingActivation() const { return _showing_activation; }
    bool isShowingClock() const { return _showing_clock; }

    /**
     * @brief Get the UI Visualizer instance
     */
    UIVisualizer* getVisualizer() const { return _ui_visualizer; }

        /**

         * @brief Get the GifManager instance

         */

    GifManager* getGifManager() const { return _gif_manager; }

    

    private:

        // Drawing helpers

        void drawStatusBar();

        void updateStatusBarCache(WifiManager* wifi_client, AXP2101* power_manager);

        void drawActivationScreen(const String& url, const String& code);

        void updateAnimation();
        void updateHapticVisualizer(SystemState current_state);
        void updateTimerFlash();
        void formatTimerText(uint32_t remaining_seconds,
                             TimerManager::DisplayFormat format,
                             char* buffer,
                             size_t buffer_size) const;

    

        // Helper to map SystemState to GIF name

        String mapSystemStateToName(SystemState state);

    

        static void animationTask(void* arg);

        DosBootAnimator* _dos_boot;


        ArduinoSSD1351* _display;

        LittleFSManager* _fs;

        AudioVisualizer* _audio_visualizer;
        Mp3Player* _mp3_player;
        HapticsManager* _haptics_manager;
        TimerManager* _timer_manager;
        AdxlManager* _adxl_manager;
        EffectsManager* _effects_manager;

        UIVisualizer* _ui_visualizer;

        GifManager* _gif_manager;
        DigitalClock* _digital_clock;

        bool _has_gifs;

        SleepAnimator* _sleep_animator;
        MotionAnimator* _motion_animator;
        ChargingAnimator* _charging_animator;
        

        // Mutex for display access protection (shared with GifPlayer)

        void* _display_mutex; // SemaphoreHandle_t

    

        // State

        bool _showing_activation;
        bool _showing_clock;

        String _activation_url;

        String _activation_code;
        bool _activation_dirty;

        

        bool _show_center_visualization;

    

        // Animation state

        volatile bool _animate_active;

        uint16_t _animation_phase;

        int _current_bar_state; // SystemState cast to int
        int _current_gif_state; // SystemState cast to int

        bool _ws_connecting;

        

        // Cached state for UI thread safety

        struct {

            uint16_t wifi_color;

            uint16_t battery_color;

            int battery_fill_width;

            bool battery_connected;

            uint8_t battery_percentage;
            bool timer_visible;
            String timer_text;
            uint32_t timer_remaining_seconds;
            TimerManager::DisplayFormat timer_display_format;

        } _status_cache;

        bool _status_bar_dirty;

    

        volatile bool _cached_ws_hello;

        bool _has_cached_audio;
        bool _timer_flash_active;
        bool _timer_flash_visible;
        uint8_t _timer_flash_remaining;
        unsigned long _timer_flash_next_ms;
        bool _timer_flash_continuous;
        HapticsVisualizer* _haptics_visualizer;
};

    
