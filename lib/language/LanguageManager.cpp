/**
 * LanguageManager.cpp
 *
 * Implementation for LanguageManager.
 */

#include "LanguageManager.h"
#include "DeviceConfig.h"
#include <esp_log.h>

static const char* TAG = "LanguageManager";

// Available languages
const char* LanguageManager::AVAILABLE_LANGUAGES[] = {"en-US", "zh-CN"};
const size_t LanguageManager::LANGUAGE_COUNT = 2;

LanguageManager::LanguageManager(LittleFSManager* filesystem)
    : _ready(false)
    , _fs(filesystem)
{
}

LanguageManager::~LanguageManager() {
}

LangStatus LanguageManager::begin() {
    ESP_LOGD(TAG, "Initializing language manager...");

    // Check if filesystem is mounted
    if (!_fs || !_fs->isMounted()) {
        ESP_LOGE(TAG, "LittleFS not mounted");
        return LangStatus::LANG_FS_NOT_MOUNTED;
    }

    // Fast boot: only check critical files
    if (!checkCriticalFiles()) {
        ESP_LOGE(TAG, "Critical language files missing");
        return LangStatus::LANG_FILE_NOT_FOUND;
    }

    // Load default language
    if (!loadLanguage(DEFAULT_LANGUAGE)) {
        ESP_LOGE(TAG, "❌ Failed to load default language");
        return LangStatus::LANG_PARSE_ERROR;
    }

    _ready = true;
    ESP_LOGI(TAG, "✅ Language manager initialized");
    return LangStatus::LANG_SUCCESS;
}

bool LanguageManager::loadLanguage(const char* lang_code) {
    if (!lang_code) {
        ESP_LOGE(TAG, "❌ Invalid language code");
        return false;
    }

    ESP_LOGD(TAG, "Loading language: %s", lang_code);

    // Build path to language file
    String lang_file = String("/lang/") + lang_code + "/language.json";

    // Check if file exists
    if (!_fs->exists(lang_file.c_str())) {
        ESP_LOGE(TAG, "Language file not found: %s", lang_file.c_str());
        return false;
    }

    // Open language file
    File file = _fs->open(lang_file.c_str(), "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ Failed to open language file: %s", lang_file.c_str());
        return false;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        ESP_LOGE(TAG, "❌ Failed to parse language file: %s", error.c_str());
        return false;
    }

    // Clear existing strings
    _strings.clear();

    // Load strings from JSON (check for nested "strings" object)
    JsonObject root = doc.as<JsonObject>();
    JsonObject strings_obj;

    if (root["strings"].is<JsonObject>()) {
        // New format: { "language": {...}, "strings": {...} }
        strings_obj = root["strings"].as<JsonObject>();
    } else {
        // Old format: direct key-value pairs
        strings_obj = root;
    }

    for (JsonPair kv : strings_obj) {
        _strings[kv.key().c_str()] = kv.value().as<String>();
    }

    // Update current language
    _current_language = lang_code;

    ESP_LOGD(TAG, "✅ Loaded %d strings for %s", _strings.size(), lang_code);
    return true;
}

const char* LanguageManager::getString(const char* key) {
    if (!_ready || !key) {
        return "";
    }

    // Look up string
    auto it = _strings.find(key);
    if (it != _strings.end()) {
        return it->second.c_str();
    }

    // Not found - return key as fallback
    ESP_LOGW(TAG, "String not found: %s", key);
    return key;
}

bool LanguageManager::setLanguage(const char* lang_code) {
    if (!_ready || !lang_code) {
        return false;
    }

    // Validate language code
    bool valid = false;
    for (size_t i = 0; i < LANGUAGE_COUNT; i++) {
        if (strcmp(lang_code, AVAILABLE_LANGUAGES[i]) == 0) {
            valid = true;
            break;
        }
    }

    if (!valid) {
        ESP_LOGE(TAG, "❌ Invalid language code: %s", lang_code);
        return false;
    }

    // Don't reload if already loaded
    if (_current_language == lang_code) {
        ESP_LOGD(TAG, "Language already loaded: %s", lang_code);
        return true;
    }

    // Load new language
    return loadLanguage(lang_code);
}

const char** LanguageManager::getAvailableLanguages(size_t& count) {
    count = LANGUAGE_COUNT;
    return AVAILABLE_LANGUAGES;
}

bool LanguageManager::checkCriticalFiles() {
    // Fast boot optimization: only check essential files for default language
    if (!_fs || !_fs->isMounted()) {
        ESP_LOGW(TAG, "Cannot check files: filesystem not mounted");
        return false;
    }

    // Check that default language directory exists
    String default_lang_dir = String("/lang/") + DEFAULT_LANGUAGE;
    if (!_fs->exists(default_lang_dir.c_str())) {
        ESP_LOGE(TAG, "Critical: Default language directory not found: %s", default_lang_dir.c_str());
        return false;
    }

    // Check that default language file exists
    String default_strings = default_lang_dir + "/language.json";
    if (!_fs->exists(default_strings.c_str())) {
        ESP_LOGE(TAG, "Critical: Default language file not found: %s", default_strings.c_str());
        return false;
    }

    ESP_LOGD(TAG, "✅  Critical language files present");
    return true;
}

bool LanguageManager::validateLanguageFiles() {
    // Full validation (deferred after boot)
    if (!_fs || !_fs->isMounted()) {
        ESP_LOGW(TAG, "Cannot validate: filesystem not mounted");
        return false;
    }

    bool all_valid = true;

    // Check all available languages
    for (size_t i = 0; i < LANGUAGE_COUNT; i++) {
        const char* lang = AVAILABLE_LANGUAGES[i];
        String lang_dir = String("/lang/") + lang;
        String lang_file = lang_dir + "/language.json";

        // Check language directory
        if (!_fs->exists(lang_dir.c_str())) {
            ESP_LOGW(TAG, "🟡 Warning: Language directory missing: %s", lang_dir.c_str());
            all_valid = false;
            continue;
        }

        // Check language.json
        if (!_fs->exists(lang_file.c_str())) {
            ESP_LOGW(TAG, "🟡 Warning: Language file missing: %s", lang_file.c_str());
            all_valid = false;
        }
    }

    if (!all_valid) {
        ESP_LOGW(TAG, "Please ensure you have uploaded the complete language files");
    } else {
        ESP_LOGI(TAG, "✅ All language files validated successfully");
    }

    return all_valid;
}

void LanguageManager::printDebugInfo() {
    ESP_LOGI(TAG, ":::: Language Manager Debug Info ::::");
    ESP_LOGI(TAG, "Filesystem mounted: %s", (_fs && _fs->isMounted()) ? "Yes" : "No");
    ESP_LOGI(TAG, "Manager ready: %s", _ready ? "Yes" : "No");
    ESP_LOGI(TAG, "Current language: %s", _current_language.c_str());
    ESP_LOGI(TAG, "Strings loaded: %d", _strings.size());
    ESP_LOGI(TAG, "Available languages:");
    for (size_t i = 0; i < LANGUAGE_COUNT; i++) {
        ESP_LOGI(TAG, "  - %s", AVAILABLE_LANGUAGES[i]);
    }
    ESP_LOGI(TAG, ":::: End Language Manager Debug Info ::::");
}
