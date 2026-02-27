/**
 * NvsStorage.cpp
 *
 * Implementation for NvsStorage.
 */

#include "NvsStorage.h"
#include <esp_log.h>

// Forward declaration
extern String generateUUID();

static const char* TAG = "NVSStorage";

// Namespace names
#define NS_WIFI      "wifi"
#define NS_AUDIO     "audio"
#define NS_SYSTEM    "system"
#define NS_DEVICE    "device"
#define NS_MQTT      "mqtt"
#define NS_WEBSOCKET "websocket"

// Key names
#define KEY_SSID        "ssid"
#define KEY_PASSWORD    "password"
#define KEY_VOLUME      "volume"
#define KEY_AUDIO_EN    "audio_en"
#define KEY_LANGUAGE    "language"
#define KEY_TIMEZONE_NAME "timezone_name"
#define KEY_LOCATION    "location"
#define KEY_WIFI_EN     "wifi_en"
#define KEY_BRIGHTNESS  "brightness"
#define KEY_EFFECTS_SCANLINES "fx_scanlines"
#define KEY_EFFECTS_GLITCH "fx_glitch"
#define KEY_EFFECTS_DOT_MATRIX "fx_dot"
#define KEY_EFFECTS_TINT "fx_tint"
#define KEY_EFFECTS_TINT_COLOR "fx_tint_color"
#define KEY_UUID        "uuid"

// MQTT key names
#define KEY_MQTT_ENDPOINT      "endpoint"
#define KEY_MQTT_CLIENT_ID     "client_id"
#define KEY_MQTT_USERNAME      "username"
#define KEY_MQTT_PASSWORD      "password"
#define KEY_MQTT_PUBLISH_TOPIC "pub_topic"
#define KEY_MQTT_KEEPALIVE     "keepalive"

// WebSocket key names
#define KEY_WS_HOST     "host"
#define KEY_WS_PORT     "port"
#define KEY_WS_PATH     "path"
#define KEY_WS_USE_SSL  "use_ssl"
#define KEY_WS_TOKEN    "token"
#define KEY_WS_VERSION  "version"

// Default values
#define DEFAULT_VOLUME      70
#define DEFAULT_AUDIO_EN    true
#define DEFAULT_LANGUAGE    "en-US"
#define DEFAULT_WIFI_EN     true
#define DEFAULT_BRIGHTNESS  80
#define DEFAULT_EFFECTS_SCANLINES false
#define DEFAULT_EFFECTS_GLITCH false
#define DEFAULT_EFFECTS_DOT_MATRIX false
#define DEFAULT_EFFECTS_TINT false
#define DEFAULT_EFFECTS_TINT_COLOR 0x07E0
#define DEFAULT_MQTT_KEEPALIVE  240

NVSStorage::NVSStorage()
    : _initialized(false) {
}

bool NVSStorage::begin() {
    ESP_LOGI(TAG, "Initializing NVS Storage");
    
    // Test open each namespace
    if (!_wifi_prefs.begin(NS_WIFI, false)) {
        ESP_LOGE(TAG, "❌ Failed to open WiFi namespace");
        return false;
    }
    _wifi_prefs.end();
    
    if (!_audio_prefs.begin(NS_AUDIO, false)) {
        ESP_LOGE(TAG, "❌ Failed to open audio namespace");
        return false;
    }
    _audio_prefs.end();
    
    if (!_system_prefs.begin(NS_SYSTEM, false)) {
        ESP_LOGE(TAG, "❌ Failed to open system namespace");
        return false;
    }
    if (!_system_prefs.isKey(KEY_LANGUAGE)) {
        if (_system_prefs.putString(KEY_LANGUAGE, DEFAULT_LANGUAGE) > 0) {
            ESP_LOGD(TAG, "System language defaulted to %s", DEFAULT_LANGUAGE);
        } else {
            ESP_LOGW(TAG, "Failed to persist default language: %s", DEFAULT_LANGUAGE);
        }
    }
    _system_prefs.end();

    if (!_device_prefs.begin(NS_DEVICE, false)) {
        ESP_LOGE(TAG, "❌ Failed to open device namespace");
        return false;
    }
    _device_prefs.end();

    if (!_mqtt_prefs.begin(NS_MQTT, false)) {
        ESP_LOGE(TAG, "❌ Failed to open MQTT namespace");
        return false;
    }
    _mqtt_prefs.end();

    if (!_websocket_prefs.begin(NS_WEBSOCKET, false)) {
        ESP_LOGE(TAG, "❌ Failed to open WebSocket namespace");
        return false;
    }
    _websocket_prefs.end();

    _initialized = true;
    ESP_LOGI(TAG, "NVS Storage initialized");

    return true;
}

bool NVSStorage::openNamespace(Preferences& prefs, const char* name, bool readonly) {
    return prefs.begin(name, readonly);
}

void NVSStorage::closeNamespace(Preferences& prefs) {
    prefs.end();
}

// WiFi Credentials
bool NVSStorage::saveWiFiCredentials(const char* ssid, const char* password) {
    if (!_initialized || !ssid || !password) {
        return false;
    }
    
    if (!openNamespace(_wifi_prefs, NS_WIFI, false)) {
        return false;
    }
    
    bool success = true;
    success &= (_wifi_prefs.putString(KEY_SSID, ssid) > 0);
    success &= (_wifi_prefs.putString(KEY_PASSWORD, password) > 0);
    
    closeNamespace(_wifi_prefs);
    
    if (success) {
        ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid);
    } else {
        ESP_LOGE(TAG, "❌ Failed to save WiFi credentials");
    }
    
    return success;
}

bool NVSStorage::loadWiFiCredentials(wifi_credentials_t* credentials) {
    if (!_initialized || !credentials) {
        return false;
    }
    
    if (!openNamespace(_wifi_prefs, NS_WIFI, true)) {
        return false;
    }
    
    String ssid = _wifi_prefs.getString(KEY_SSID, "");
    String password = _wifi_prefs.getString(KEY_PASSWORD, "");
    
    closeNamespace(_wifi_prefs);
    
    if (ssid.length() == 0) {
        ESP_LOGW(TAG, "🟡 No WiFi credentials found");
        return false;
    }
    
    strncpy(credentials->ssid, ssid.c_str(), sizeof(credentials->ssid) - 1);
    credentials->ssid[sizeof(credentials->ssid) - 1] = '\0';
    
    strncpy(credentials->password, password.c_str(), sizeof(credentials->password) - 1);
    credentials->password[sizeof(credentials->password) - 1] = '\0';
    
    ESP_LOGD(TAG, "WiFi credentials loaded: %s", credentials->ssid);
    
    return true;
}

bool NVSStorage::hasWiFiCredentials() {
    if (!_initialized) {
        return false;
    }
    
    if (!openNamespace(_wifi_prefs, NS_WIFI, true)) {
        return false;
    }
    
    bool has_ssid = _wifi_prefs.isKey(KEY_SSID);
    
    closeNamespace(_wifi_prefs);
    
    return has_ssid;
}

bool NVSStorage::clearWiFiCredentials() {
    if (!_initialized) {
        return false;
    }
    
    if (!openNamespace(_wifi_prefs, NS_WIFI, false)) {
        return false;
    }
    
    bool success = true;
    success &= _wifi_prefs.remove(KEY_SSID);
    success &= _wifi_prefs.remove(KEY_PASSWORD);
    
    closeNamespace(_wifi_prefs);
    
    if (success) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    }
    
    return success;
}

// Audio Settings
bool NVSStorage::saveAudioSettings(const audio_settings_t* settings) {
    if (!_initialized || !settings) {
        return false;
    }
    
    if (!openNamespace(_audio_prefs, NS_AUDIO, false)) {
        return false;
    }
    
    bool success = true;
    success &= (_audio_prefs.putUChar(KEY_VOLUME, settings->volume) > 0);
    success &= (_audio_prefs.putBool(KEY_AUDIO_EN, settings->enabled) > 0);
    
    closeNamespace(_audio_prefs);
    
    if (success) {
        ESP_LOGD(TAG, "Audio settings saved: vol=%d, en=%d",
                 settings->volume, settings->enabled);
    }
    
    return success;
}

bool NVSStorage::loadAudioSettings(audio_settings_t* settings) {
    if (!_initialized || !settings) {
        return false;
    }
    
    if (!openNamespace(_audio_prefs, NS_AUDIO, true)) {
        return false;
    }
    
    settings->volume = _audio_prefs.getUChar(KEY_VOLUME, DEFAULT_VOLUME);
    settings->enabled = _audio_prefs.getBool(KEY_AUDIO_EN, DEFAULT_AUDIO_EN);
    
    closeNamespace(_audio_prefs);
    
    ESP_LOGD(TAG, "Audio settings loaded: vol=%d, en=%d",
             settings->volume, settings->enabled);
    
    return true;
}

// System Settings
bool NVSStorage::saveSystemSettings(const system_settings_t* settings) {
    if (!_initialized || !settings) {
        return false;
    }
    
    if (!openNamespace(_system_prefs, NS_SYSTEM, false)) {
        return false;
    }
    
    bool success = true;
    success &= (_system_prefs.putString(KEY_LANGUAGE, settings->language) > 0);
    success &= (_system_prefs.putBool(KEY_WIFI_EN, settings->wifi_enabled) > 0);
    success &= (_system_prefs.putUChar(KEY_BRIGHTNESS, settings->brightness) > 0);
    success &= (_system_prefs.putBool(KEY_EFFECTS_SCANLINES, settings->effects_scanlines_enabled) > 0);
    success &= (_system_prefs.putBool(KEY_EFFECTS_GLITCH, settings->effects_glitch_enabled) > 0);
    success &= (_system_prefs.putBool(KEY_EFFECTS_DOT_MATRIX, settings->effects_dot_matrix_enabled) > 0);
    success &= (_system_prefs.putBool(KEY_EFFECTS_TINT, settings->effects_tint_enabled) > 0);
    success &= (_system_prefs.putUShort(KEY_EFFECTS_TINT_COLOR, settings->effects_tint_color) > 0);
    
    closeNamespace(_system_prefs);
    
    if (success) {
        ESP_LOGD(TAG, "System settings saved: lang=%s, wifi=%d, bright=%d",
                 settings->language, settings->wifi_enabled, settings->brightness);
    }
    
    return success;
}

bool NVSStorage::loadSystemSettings(system_settings_t* settings) {
    if (!_initialized || !settings) {
        return false;
    }
    
    if (!openNamespace(_system_prefs, NS_SYSTEM, true)) {
        return false;
    }
    
    String language = _system_prefs.getString(KEY_LANGUAGE, DEFAULT_LANGUAGE);
    strncpy(settings->language, language.c_str(), sizeof(settings->language) - 1);
    settings->language[sizeof(settings->language) - 1] = '\0';
    
    settings->wifi_enabled = _system_prefs.getBool(KEY_WIFI_EN, DEFAULT_WIFI_EN);
    settings->brightness = _system_prefs.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    settings->effects_scanlines_enabled = _system_prefs.getBool(KEY_EFFECTS_SCANLINES, DEFAULT_EFFECTS_SCANLINES);
    settings->effects_glitch_enabled = _system_prefs.getBool(KEY_EFFECTS_GLITCH, DEFAULT_EFFECTS_GLITCH);
    settings->effects_dot_matrix_enabled = _system_prefs.getBool(KEY_EFFECTS_DOT_MATRIX, DEFAULT_EFFECTS_DOT_MATRIX);
    settings->effects_tint_enabled = _system_prefs.getBool(KEY_EFFECTS_TINT, DEFAULT_EFFECTS_TINT);
    settings->effects_tint_color = _system_prefs.getUShort(KEY_EFFECTS_TINT_COLOR, DEFAULT_EFFECTS_TINT_COLOR);
    
    closeNamespace(_system_prefs);
    
    ESP_LOGD(TAG, "System settings loaded: lang=%s, wifi=%d, bright=%d",
             settings->language, settings->wifi_enabled, settings->brightness);
    
    return true;
}

// Convenience Functions
bool NVSStorage::setVolume(uint8_t volume) {
    audio_settings_t settings;
    loadAudioSettings(&settings);
    settings.volume = constrain(volume, 0, 100);
    return saveAudioSettings(&settings);
}

uint8_t NVSStorage::getVolume() {
    audio_settings_t settings;
    loadAudioSettings(&settings);
    return settings.volume;
}

bool NVSStorage::setAudioEnabled(bool enabled) {
    audio_settings_t settings;
    loadAudioSettings(&settings);
    settings.enabled = enabled;
    return saveAudioSettings(&settings);
}

bool NVSStorage::setEffectsScanlinesEnabled(bool enabled) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.effects_scanlines_enabled = enabled;
    return saveSystemSettings(&settings);
}

bool NVSStorage::getEffectsScanlinesEnabled() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.effects_scanlines_enabled;
}

bool NVSStorage::setEffectsGlitchEnabled(bool enabled) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.effects_glitch_enabled = enabled;
    return saveSystemSettings(&settings);
}

bool NVSStorage::getEffectsGlitchEnabled() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.effects_glitch_enabled;
}

bool NVSStorage::setEffectsDotMatrixEnabled(bool enabled) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.effects_dot_matrix_enabled = enabled;
    return saveSystemSettings(&settings);
}

bool NVSStorage::getEffectsDotMatrixEnabled() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.effects_dot_matrix_enabled;
}

bool NVSStorage::setEffectsTintEnabled(bool enabled) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.effects_tint_enabled = enabled;
    return saveSystemSettings(&settings);
}

bool NVSStorage::getEffectsTintEnabled() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.effects_tint_enabled;
}

bool NVSStorage::setEffectsTintColor(uint16_t color) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.effects_tint_color = color;
    return saveSystemSettings(&settings);
}

uint16_t NVSStorage::getEffectsTintColor() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.effects_tint_color;
}

bool NVSStorage::getAudioEnabled() {
    audio_settings_t settings;
    loadAudioSettings(&settings);
    return settings.enabled;
}


bool NVSStorage::setLanguage(const char* language) {
    if (!language) return false;
    
    system_settings_t settings;
    loadSystemSettings(&settings);
    strncpy(settings.language, language, sizeof(settings.language) - 1);
    settings.language[sizeof(settings.language) - 1] = '\0';
    return saveSystemSettings(&settings);
}

String NVSStorage::getLanguage() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return String(settings.language);
}

bool NVSStorage::setTimezoneName(const char* timezone_name) {
    if (!_initialized || !timezone_name) {
        return false;
    }

    if (!openNamespace(_system_prefs, NS_SYSTEM, false)) {
        return false;
    }

    bool success = true;
    if (timezone_name[0] == '\0') {
        success = _system_prefs.remove(KEY_TIMEZONE_NAME);
    } else {
        success = (_system_prefs.putString(KEY_TIMEZONE_NAME, timezone_name) > 0);
    }
    closeNamespace(_system_prefs);

    if (success) {
        ESP_LOGI(TAG, "Timezone name saved: %s", timezone_name);
    } else {
        ESP_LOGW(TAG, "🟡 Failed to save timezone name");
    }

    return success;
}

String NVSStorage::getTimezoneName() {
    if (!_initialized) {
        return String("");
    }

    if (!openNamespace(_system_prefs, NS_SYSTEM, true)) {
        return String("");
    }

    String timezone_name = _system_prefs.getString(KEY_TIMEZONE_NAME, "");
    closeNamespace(_system_prefs);
    return timezone_name;
}

bool NVSStorage::setLocation(const char* location) {
    if (!_initialized || !location) {
        return false;
    }

    if (!openNamespace(_system_prefs, NS_SYSTEM, false)) {
        return false;
    }

    bool success = true;
    if (location[0] == '\0') {
        success = _system_prefs.remove(KEY_LOCATION);
    } else {
        success = (_system_prefs.putString(KEY_LOCATION, location) > 0);
    }
    closeNamespace(_system_prefs);

    if (success) {
        ESP_LOGI(TAG, "Location saved: %s", location);
    } else {
        ESP_LOGW(TAG, "🟡 Failed to save location");
    }

    return success;
}

String NVSStorage::getLocation() {
    if (!_initialized) {
        return String("");
    }

    if (!openNamespace(_system_prefs, NS_SYSTEM, true)) {
        return String("");
    }

    String location = _system_prefs.getString(KEY_LOCATION, "");
    closeNamespace(_system_prefs);
    return location;
}

bool NVSStorage::setWiFiEnabled(bool enabled) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.wifi_enabled = enabled;
    return saveSystemSettings(&settings);
}

bool NVSStorage::getWiFiEnabled() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.wifi_enabled;
}

bool NVSStorage::setBrightness(uint8_t brightness) {
    system_settings_t settings;
    loadSystemSettings(&settings);
    settings.brightness = constrain(brightness, 0, 100);
    return saveSystemSettings(&settings);
}

uint8_t NVSStorage::getBrightness() {
    system_settings_t settings;
    loadSystemSettings(&settings);
    return settings.brightness;
}

bool NVSStorage::setDeviceUUID(const char* uuid) {
    if (!_initialized || !uuid) {
        return false;
    }

    if (!openNamespace(_device_prefs, NS_DEVICE, false)) {
        return false;
    }

    bool success = (_device_prefs.putString(KEY_UUID, uuid) > 0);

    closeNamespace(_device_prefs);

    if (success) {
        // Update cache when UUID is successfully saved
        _cachedUUID = String(uuid);
        ESP_LOGI(TAG, "Device UUID saved: %s", uuid);
    } else {
        ESP_LOGE(TAG, "❌ Failed to save device UUID");
    }

    return success;
}

String NVSStorage::getDeviceUUID() {
    // Return cached value if available
    if (_cachedUUID.length() > 0) {
        return _cachedUUID;
    }

    if (!_initialized) {
        return String("");
    }

    if (!openNamespace(_device_prefs, NS_DEVICE, true)) {
        return String("");
    }

    _cachedUUID = _device_prefs.getString(KEY_UUID, "");

    closeNamespace(_device_prefs);

    // If no UUID exists, generate and save one
    if (_cachedUUID.isEmpty()) {
        _cachedUUID = generateUUID();
        if (setDeviceUUID(_cachedUUID.c_str())) {
            ESP_LOGI(TAG, "Generated new UUID: %s", _cachedUUID.c_str());
        } else {
            ESP_LOGE(TAG, "❌ Failed to save generated UUID");
            _cachedUUID = "";  // Clear cache on failure
        }
    }

    return _cachedUUID;
}

// Audio Configuration (Simple Preferences-style access)
bool NVSStorage::beginAudio(bool readonly) {
    if (!_initialized) {
        return false;
    }
    return openNamespace(_audio_prefs, NS_AUDIO, readonly);
}

void NVSStorage::endAudio() {
    closeNamespace(_audio_prefs);
}

// MQTT Configuration (Simple Preferences-style access)
bool NVSStorage::beginMqtt(bool readonly) {
    if (!_initialized) {
        return false;
    }
    return openNamespace(_mqtt_prefs, NS_MQTT, readonly);
}

void NVSStorage::endMqtt() {
    closeNamespace(_mqtt_prefs);
}

// WebSocket Configuration (Simple Preferences-style access)
bool NVSStorage::beginWebSocket(bool readonly) {
    if (!_initialized) {
        return false;
    }
    return openNamespace(_websocket_prefs, NS_WEBSOCKET, readonly);
}

void NVSStorage::endWebSocket() {
    closeNamespace(_websocket_prefs);
}

// Maintenance
bool NVSStorage::resetToDefaults() {
    if (!_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "Resetting all settings to defaults");
    
    // Reset audio
    audio_settings_t audio = {
        .volume = DEFAULT_VOLUME,
        .enabled = DEFAULT_AUDIO_EN
    };
    saveAudioSettings(&audio);
    
    // Reset system - initialize then set string
    system_settings_t system;
    system.wifi_enabled = DEFAULT_WIFI_EN;
    system.brightness = DEFAULT_BRIGHTNESS;
    system.effects_scanlines_enabled = DEFAULT_EFFECTS_SCANLINES;
    system.effects_glitch_enabled = DEFAULT_EFFECTS_GLITCH;
    system.effects_dot_matrix_enabled = DEFAULT_EFFECTS_DOT_MATRIX;
    system.effects_tint_enabled = DEFAULT_EFFECTS_TINT;
    system.effects_tint_color = DEFAULT_EFFECTS_TINT_COLOR;
    strncpy(system.language, DEFAULT_LANGUAGE, sizeof(system.language) - 1);
    system.language[sizeof(system.language) - 1] = '\0';
    saveSystemSettings(&system);
    
    ESP_LOGI(TAG, "All settings reset to defaults");
    
    return true;
}

bool NVSStorage::clearAll() {
    if (!_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "Clearing all NVS data");
    
    bool success = true;
    
    if (openNamespace(_wifi_prefs, NS_WIFI, false)) {
        success &= _wifi_prefs.clear();
        closeNamespace(_wifi_prefs);
    }
    
    if (openNamespace(_audio_prefs, NS_AUDIO, false)) {
        success &= _audio_prefs.clear();
        closeNamespace(_audio_prefs);
    }
    
    if (openNamespace(_system_prefs, NS_SYSTEM, false)) {
        success &= _system_prefs.clear();
        closeNamespace(_system_prefs);
    }

    if (openNamespace(_mqtt_prefs, NS_MQTT, false)) {
        success &= _mqtt_prefs.clear();
        closeNamespace(_mqtt_prefs);
    }

    if (success) {
        ESP_LOGI(TAG, "All NVS data cleared");
    } else {
        ESP_LOGE(TAG, "❌ Failed to clear some NVS data");
    }

    return success;
}

void NVSStorage::printDebugInfo() {
    if (!_initialized) {
        ESP_LOGW(TAG, "NVS not initialized");
        return;
    }
    
    ESP_LOGI(TAG, ":::: NVS Storage Debug Info ::::");
    
    // WiFi
    wifi_credentials_t wifi;
    if (loadWiFiCredentials(&wifi)) {
        ESP_LOGI(TAG, "WiFi SSID: %s", wifi.ssid);
        ESP_LOGI(TAG, "WiFi Password: [HIDDEN]");
    } else {
        ESP_LOGI(TAG, "WiFi: No credentials");
    }
    
    // Audio
    audio_settings_t audio;
    loadAudioSettings(&audio);
    ESP_LOGI(TAG, "Audio: volume=%d, enabled=%d",
             audio.volume, audio.enabled);
    
    // System
    system_settings_t system;
    loadSystemSettings(&system);
    ESP_LOGI(TAG, "System: lang=%s, wifi_en=%d, brightness=%d",
             system.language, system.wifi_enabled, system.brightness);
    ESP_LOGI(TAG, "System: timezone_name=%s", getTimezoneName().c_str());
    ESP_LOGI(TAG, "System: location=%s", getLocation().c_str());

    // MQTT
    if (beginMqtt(true)) {
        String endpoint = _mqtt_prefs.getString("endpoint", "");
        if (endpoint.length() > 0) {
            String client_id = _mqtt_prefs.getString("client_id", "");
            int keepalive = _mqtt_prefs.getInt("keepalive", 0);
            String username = _mqtt_prefs.getString("username", "");
            String publish_topic = _mqtt_prefs.getString("pub_topic", "");

            ESP_LOGI(TAG, "MQTT: endpoint=%s, client_id=%s, keepalive=%d",
                     endpoint.c_str(), client_id.c_str(), keepalive);
            ESP_LOGI(TAG, "MQTT: username=%s, password=[HIDDEN]", username.c_str());
            ESP_LOGI(TAG, "MQTT: publish_topic=%s", publish_topic.c_str());
        } else {
            ESP_LOGI(TAG, "MQTT: No configuration");
        }
        endMqtt();
    } else {
        ESP_LOGI(TAG, "MQTT: No configuration");
    }

    ESP_LOGI(TAG, ":::: End NVS Storage Debug Info ::::");
}
