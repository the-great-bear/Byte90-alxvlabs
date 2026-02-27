/**
 * Application.h
 *
 * Declarations for Application.
 */

#pragma once

#include <WString.h>

// Project includes
#include "DeviceConfig.h"

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;
/**
 * @brief DeviceInfo.
 */
class DeviceInfo;
/**
 * @brief LittleFSManager.
 */
class LittleFSManager;
/**
 * @brief WifiManager.
 */
class WifiManager;
/**
 * @brief SerialClient.
 */
class SerialClient;
/**
 * @brief SystemStateManager.
 */
class SystemStateManager;
/**
 * @brief AXP2101.
 */
class AXP2101;
/**
 * @brief HapticsManager.
 */
class HapticsManager;
/**
 * @brief LanguageManager.
 */
class LanguageManager;
/**
 * @brief McpServer.
 */
class McpServer;
/**
 * @brief ApplicationAudio.
 */
class ApplicationAudio;
/**
 * @brief ApplicationUI.
 */
class ApplicationUI;
/**
 * @brief ApplicationServices.
 */
class ApplicationServices;
/**
 * @brief EventBus.
 */
class EventBus;
/**
 * @brief InputManager.
 */
class InputManager;
/**
 * @brief WebSocketClient.
 */
class WebSocketClient;
/**
 * @brief AudioCodec.
 */
class AudioCodec;
/**
 * @brief AudioService.
 */
class AudioService;
/**
 * @brief Adxl345.
 */
class Adxl345;
/**
 * @brief AdxlManager.
 */
class AdxlManager;
/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351;
class ToneGenerator;
/**
 * @brief TimerManager.
 */
class TimerManager;
class EffectsManager;

/**
 * Application - Main application controller
 *
 * Features:
 * - Component lifecycle management
 * - Initialization orchestration
 * - Main loop coordination
 * - Global state management
 *
 * Architecture:
 * - Owns all major subsystem instances
 * - Provides component accessor methods for MCP tools
 * - Handles button events and protocol connections
 * - Coordinates between audio, network, and state subsystems
 */
class Application {
public:
    /**
     * @brief Construct application instance with hardware components
     *
     * @param storage NVS storage instance
     * @param deviceInfo Device information instance
     * @param filesystem LittleFS manager instance
     * @param wifiClient WiFi manager instance
     * @param serialClient Serial client instance
     * @param stateManager System state manager instance
     * @param powerManager Power manager instance (AXP2101)
     * @param hapticsManager Haptics manager instance (optional)
     * @param audioCodec Audio codec instance
     * @param languageManager Language manager instance
     * @param display Display instance (ArduinoSSD1351)
     * @param adxl ADXL345 instance
     * @param adxlManager ADXL motion manager instance
     */
    Application(
        NVSStorage* storage,
        LittleFSManager* filesystem,
        WifiManager* wifiClient,
        SerialClient* serialClient,
        SystemStateManager* stateManager,
        AXP2101* powerManager,
        HapticsManager* hapticsManager,
        AudioCodec* audioCodec,
        LanguageManager* languageManager,
        ArduinoSSD1351* display,
        Adxl345* adxl,
        AdxlManager* adxlManager
    );

    /**
     * @brief Destroy application and cleanup resources
     */
    ~Application();

    /**
     * @brief Initialize application subsystems
     *
     * Call this once during setup() after hardware is initialized.
     */
    void initialize();

    /**
     * @brief Main application update loop
     *
     * Call this repeatedly from loop().
     */
    void loop();

private:
    // Initialization methods
    void initializeLanguage();
    void initializeButton();
    void initializeProtocol();
    void initializeMCP();
    void updateOpenAiKeyActivationPrompt(bool force_check = false);

    // Update methods
    void updateAudioTransmission();

    // Component instances
    NVSStorage* _storage;
    LittleFSManager* _filesystem;
    WifiManager* _wifi_client;
    SerialClient* _serial_client;
    SystemStateManager* _state_manager;
    AXP2101* _power_manager;
    HapticsManager* _haptics_manager;
    LanguageManager* _language_manager;
    McpServer* _mcp_server;
    ArduinoSSD1351* _display;
    Adxl345* _adxl;
    AdxlManager* _adxl_manager;
    TimerManager* _timer_manager;

    // Subsystems
    ApplicationAudio* _audio;
    ApplicationUI* _ui;
    ApplicationServices* _network;
    EventBus* _event_bus;
    InputManager* _input_manager;
    ToneGenerator* _tone_generator;
    EffectsManager* _effects_manager;

    // Timing
    unsigned long _last_tick;
    unsigned long _last_status_log;

    // Protocol connection tracking
    bool _protocol_connected;
    bool _protocol_ready;
    bool _ws_connected;
    bool _pending_listening_start;
    bool _shutdown_pending;
    unsigned long _shutdown_ready_ms;
    unsigned long _connecting_sound_last_ms;
    bool _connecting_sound_active;
    bool _pending_connect_ui_update;
    int _timer_alert_remaining;
    bool _timer_alert_active;
    bool _openai_key_prompt_dismissed;
    unsigned long _openai_key_last_check_ms;
};
