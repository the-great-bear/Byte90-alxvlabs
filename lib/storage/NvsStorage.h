/**
 * NvsStorage.h
 *
 * Declarations for NvsStorage.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <Preferences.h>

// Type definitions

/**
 * WiFi credentials structure
 */
typedef struct {
    char ssid[64];
    char password[64];
} wifi_credentials_t;

/**
 * Audio settings structure
 */
typedef struct {
    uint8_t volume;          // 0-100
    bool enabled;
} audio_settings_t;

/**
 * System settings structure
 */
typedef struct {
    char language[8];        // "zh-CN", "en-US", etc.
    bool wifi_enabled;
    uint8_t brightness;      // 0-100
    bool effects_scanlines_enabled;
    bool effects_glitch_enabled;
    bool effects_dot_matrix_enabled;
    bool effects_tint_enabled;
    uint16_t effects_tint_color;
} system_settings_t;

/**
 * WebSocket configuration structure
 */
typedef struct {
    char host[128];          // WebSocket host
    int port;                // WebSocket port
    char path[128];          // WebSocket path
    bool use_ssl;            // Whether to use SSL/TLS
    char token[256];         // Authentication token
    int version;             // Protocol version
} websocket_config_t;

/**
 * NVSStorage - Non-Volatile Storage manager
 *
 * Features:
 * - WiFi credentials storage
 * - Audio settings persistence
 * - System configuration
 * - WebSocket configuration
 * - Device UUID management
 *
 * Uses ESP32 NVS (Non-Volatile Storage) via Preferences library.
 */
class NVSStorage {
public:
    /**
     * @brief Construct NVS storage manager instance
     */
    NVSStorage();

    /**
     * @brief Initialize and start the component
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Check if storage manager is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    // WiFi credentials

    /**
     * @brief Save WiFi credentials
     *
     * @param ssid WiFi network name
     * @param password WiFi password
     * @return true on success, false on failure
     */
    bool saveWiFiCredentials(const char* ssid, const char* password);

    /**
     * @brief Load WiFi credentials
     *
     * @param credentials Pointer to credentials structure
     * @return true on success, false on failure
     */
    bool loadWiFiCredentials(wifi_credentials_t* credentials);

    /**
     * @brief Check if WiFi credentials are stored
     *
     * @return true if credentials exist, false otherwise
     */
    bool hasWiFiCredentials();

    /**
     * @brief Clear WiFi credentials
     *
     * @return true on success, false on failure
     */
    bool clearWiFiCredentials();

    // Audio settings

    /**
     * @brief Save audio settings
     *
     * @param settings Pointer to audio settings structure
     * @return true on success, false on failure
     */
    bool saveAudioSettings(const audio_settings_t* settings);

    /**
     * @brief Load audio settings
     *
     * @param settings Pointer to audio settings structure
     * @return true on success, false on failure
     */
    bool loadAudioSettings(audio_settings_t* settings);

    // Audio settings (Preferences-style simple access)

    /**
     * @brief Begin Audio namespace for read/write
     *
     * @param readonly true for read-only access
     * @return true on success, false on failure
     */
    bool beginAudio(bool readonly = false);

    /**
     * @brief End Audio namespace
     */
    void endAudio();

    /**
     * @brief Get Audio Preferences object for direct access
     *
     * @return Reference to Audio Preferences object
     */
    Preferences& getAudioPrefs() { return _audio_prefs; }

    // System settings

    /**
     * @brief Save system settings
     *
     * @param settings Pointer to system settings structure
     * @return true on success, false on failure
     */
    bool saveSystemSettings(const system_settings_t* settings);

    /**
     * @brief Load system settings
     *
     * @param settings Pointer to system settings structure
     * @return true on success, false on failure
     */
    bool loadSystemSettings(system_settings_t* settings);

    // WebSocket configuration (Preferences-style simple access)

    /**
     * @brief Begin WebSocket namespace for read/write
     *
     * @param readonly true for read-only access
     * @return true on success, false on failure
     */
    bool beginWebSocket(bool readonly = false);

    /**
     * @brief End WebSocket namespace
     */
    void endWebSocket();

    /**
     * @brief Get WebSocket Preferences object for direct access
     *
     * @return Reference to WebSocket Preferences object
     */
    Preferences& getWebSocketPrefs() { return _websocket_prefs; }

    // OpenAI API key

    /**
     * @brief Save the OpenAI API key
     *
     * @param api_key API key string
     * @return true on success, false on failure
     */
    bool saveOpenAiApiKey(const char* api_key);

    /**
     * @brief Clear the OpenAI API key
     *
     * @return true on success, false on failure
     */
    bool clearOpenAiApiKey();

    /**
     * @brief Check if an OpenAI API key is stored
     *
     * @return true if key exists, false otherwise
     */
    bool hasOpenAiApiKey();

    /**
     * @brief Get the last 4 characters of the stored OpenAI API key
     *
     * @return Last 4 characters or empty string if missing/too short
     */
    String getOpenAiApiKeyLast4();

    /**
     * @brief Get the stored OpenAI API key
     *
     * @return API key string or empty if missing
     */
    String getOpenAiApiKey();

    // Gemini API key

    /**
     * @brief Save the Gemini API key
     *
     * @param api_key API key string
     * @return true on success, false on failure
     */
    bool saveGeminiApiKey(const char* api_key);

    /**
     * @brief Clear the Gemini API key
     *
     * @return true on success, false on failure
     */
    bool clearGeminiApiKey();

    /**
     * @brief Check if a Gemini API key is stored
     *
     * @return true if key exists, false otherwise
     */
    bool hasGeminiApiKey();

    /**
     * @brief Get the last 4 characters of the stored Gemini API key
     *
     * @return Last 4 characters or empty string if missing/too short
     */
    String getGeminiApiKeyLast4();

    /**
     * @brief Get the stored Gemini API key
     *
     * @return API key string or empty if missing
     */
    String getGeminiApiKey();

    // Convenience getters/setters

    /**
     * @brief Set the output volume
     *
     * @param volume Volume level (0-100)
     * @return true on success, false on failure
     */
    bool setVolume(uint8_t volume);

    /**
     * @brief Get the output volume
     *
     * @return Volume level (0-100)
     */
    uint8_t getVolume();

    /**
     * @brief Enable or disable audio output
     *
     * @param enabled true to enable audio, false to mute
     * @return true on success, false on failure
     */
    bool setAudioEnabled(bool enabled);

    /**
     * @brief Check if audio output is enabled
     *
     * @return true if enabled, false if muted
     */
    bool getAudioEnabled();


    /**
     * @brief Set the system language
     *
     * @param language Language code (e.g., "zh-CN", "en-US")
     * @return true on success, false on failure
     */
    bool setLanguage(const char* language);

    /**
     * @brief Get the system language
     *
     * @return Language code string
     */
    String getLanguage();

    /**
     * @brief Set the timezone name for MCP tools
     *
     * @param timezone_name Timezone name (e.g., "North_America_Eastern")
     * @return true on success, false on failure
     */
    bool setTimezoneName(const char* timezone_name);

    /**
     * @brief Get the saved timezone name
     *
     * @return Timezone name string
     */
    String getTimezoneName();

    /**
     * @brief Set the default location for MCP tools
     *
     * @param location Location string (e.g., "Toronto")
     * @return true on success, false on failure
     */
    bool setLocation(const char* location);

    /**
     * @brief Get the saved default location
     *
     * @return Location string
     */
    String getLocation();

    /**
     * @brief Set WiFi enabled state
     *
     * @param enabled true to enable WiFi, false to disable
     * @return true on success, false on failure
     */
    bool setWiFiEnabled(bool enabled);

    /**
     * @brief Get WiFi enabled state
     *
     * @return true if WiFi enabled, false otherwise
     */
    bool getWiFiEnabled();

    /**
     * @brief Set display brightness (0-100)
     *
     * @param brightness Brightness percentage
     * @return true on success, false on failure
     */
    bool setBrightness(uint8_t brightness);

    /**
     * @brief Get display brightness (0-100)
     *
     * @return Brightness percentage
     */
    uint8_t getBrightness();

    /**
     * @brief Set scanline effect enabled state
     *
     * @param enabled true to enable, false to disable
     * @return true on success, false on failure
     */
    bool setEffectsScanlinesEnabled(bool enabled);

    /**
     * @brief Get scanline effect enabled state
     *
     * @return true if enabled, false otherwise
     */
    bool getEffectsScanlinesEnabled();

    /**
     * @brief Set glitch effect enabled state
     *
     * @param enabled true to enable, false to disable
     * @return true on success, false on failure
     */
    bool setEffectsGlitchEnabled(bool enabled);

    /**
     * @brief Get glitch effect enabled state
     *
     * @return true if enabled, false otherwise
     */
    bool getEffectsGlitchEnabled();

    /**
     * @brief Set dot matrix effect enabled state
     *
     * @param enabled true to enable, false to disable
     * @return true on success, false on failure
     */
    bool setEffectsDotMatrixEnabled(bool enabled);

    /**
     * @brief Get dot matrix effect enabled state
     *
     * @return true if enabled, false otherwise
     */
    bool getEffectsDotMatrixEnabled();

    /**
     * @brief Set tint effect enabled state
     *
     * @param enabled true to enable, false to disable
     * @return true on success, false on failure
     */
    bool setEffectsTintEnabled(bool enabled);

    /**
     * @brief Get tint effect enabled state
     *
     * @return true if enabled, false otherwise
     */
    bool getEffectsTintEnabled();

    /**
     * @brief Set tint effect color (RGB565)
     *
     * @param color Tint color in RGB565
     * @return true on success, false on failure
     */
    bool setEffectsTintColor(uint16_t color);

    /**
     * @brief Get tint effect color (RGB565)
     *
     * @return Tint color in RGB565
     */
    uint16_t getEffectsTintColor();

    // Device UUID

    /**
     * @brief Set the device UUID
     *
     * @param uuid UUID string
     * @return true on success, false on failure
     */
    bool setDeviceUUID(const char* uuid);

    /**
     * @brief Get the device UUID
     *
     * @return UUID string
     */
    String getDeviceUUID();

    // Maintenance

    /**
     * @brief Reset all settings to factory defaults
     *
     * @return true on success, false on failure
     */
    bool resetToDefaults();

    /**
     * @brief Clear all stored data
     *
     * @return true on success, false on failure
     */
    bool clearAll();

    /**
     * @brief Print debug information about stored data
     */
    void printDebugInfo();

private:
    /**
     * @brief Open NVS namespace
     *
     * @param prefs Preferences object
     * @param name Namespace name
     * @param readonly true for read-only access
     * @return true on success, false on failure
     */
    bool openNamespace(Preferences& prefs, const char* name, bool readonly);

    /**
     * @brief Close NVS namespace
     *
     * @param prefs Preferences object
     */
    void closeNamespace(Preferences& prefs);

    // NVS namespaces
    Preferences _wifi_prefs;
    Preferences _audio_prefs;
    Preferences _system_prefs;
    Preferences _device_prefs;
    Preferences _websocket_prefs;
    Preferences _openai_prefs;
    Preferences _gemini_prefs;

    // State
    bool _initialized;

    // UUID cache (loaded once on first getDeviceUUID() call)
    String _cachedUUID;
};
