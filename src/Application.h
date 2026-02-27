/**
 * Application.h
 *
 * Declarations for Application.
 */

#pragma once

#include <WString.h>

// Project includes
#include "DeviceConfig.h"
#include "ProtocolFactory.h"

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
 * @brief TenclassClient.
 */
class TenclassClient;
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
 * @brief ApiClient.
 */
class ApiClient;
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
 * @brief ProtocolConfig.
 */
struct ProtocolConfig;

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
     * @param provisioningClient Provisioning client instance
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
        TenclassClient* provisioningClient,
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

    // Update methods
    void updateAudioTransmission();
    void updateProvisioning();

    // Component instances
    NVSStorage* _storage;
    LittleFSManager* _filesystem;
    WifiManager* _wifi_client;
    SerialClient* _serial_client;
    SystemStateManager* _state_manager;
    TenclassClient* _provisioning_client;
    AXP2101* _power_manager;
    HapticsManager* _haptics_manager;
    ProtocolType* _protocol;
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

    // Protocol and state
    ProtocolConfig* _protocol_config;

    // Timing
    unsigned long _last_tick;
    unsigned long _last_status_log;

    // Protocol connection tracking
    bool _config_checked;
    bool _config_check_in_progress;
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
};
