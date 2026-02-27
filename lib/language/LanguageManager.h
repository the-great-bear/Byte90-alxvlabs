/**
 * LanguageManager.h
 *
 * Declarations for LanguageManager.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <map>

// Project includes
#include "LittlefsManager.h"

/**
 * Language system status codes
 *
 * Status codes for language file operations (inspired by FSStatus pattern)
 */
enum class LangStatus {
    LANG_SUCCESS,            // Operation successful
    LANG_FS_NOT_MOUNTED,     // Filesystem not mounted
    LANG_FILE_NOT_FOUND,     // Language file not found
    LANG_PARSE_ERROR,        // JSON parse error
    LANG_INVALID_LANGUAGE    // Unsupported language code
};

/**
 * LanguageManager - Runtime language system
 *
 * Features:
 * - Localized string management
 * - Runtime language switching
 * - Language validation
 *
 * Supported languages: en-US, zh-CN
 *
 * Architecture:
 * - Loads JSON strings from LittleFS assets partition
 * - Fast critical file validation for boot optimization
 */
class LanguageManager {
public:
    /**
     * @brief Construct language manager instance
     *
     * @param filesystem Pointer to LittleFS manager
     */
    LanguageManager(LittleFSManager* filesystem);

    /**
     * @brief Destroy language manager and free resources
     */
    ~LanguageManager();

    // Initialization

    /**
     * @brief Initialize and start the language system
     *
     * @return LangStatus indicating success or failure reason
     */
    LangStatus begin();

    /**
     * @brief Load language strings from file
     *
     * @param lang_code Language code (e.g., "en-US", "zh-CN")
     * @return true on success, false on failure
     */
    bool loadLanguage(const char* lang_code);

    // String access

    /**
     * @brief Get localized string by key
     *
     * @param key String key to look up
     * @return Localized string, or key if not found
     */
    const char* getString(const char* key);

    /**
     * @brief Get the current language code
     *
     * @return Current language code string
     */
    const char* getCurrentLanguage() const { return _current_language.c_str(); }

    // Language management

    /**
     * @brief Set the active language
     *
     * @param lang_code Language code (e.g., "en-US", "zh-CN")
     * @return true on success, false on failure
     */
    bool setLanguage(const char* lang_code);

    /**
     * @brief Get list of available languages
     *
     * @param count Output parameter for language count
     * @return Array of language code strings
     */
    const char** getAvailableLanguages(size_t& count);

    // Validation (fast boot optimization pattern)

    /**
     * @brief Check critical language files exist (fast check)
     *
     * @return true if essential files present, false otherwise
     */
    bool checkCriticalFiles();

    /**
     * @brief Validate all language files (full check)
     *
     * @return true if all files valid, false otherwise
     */
    bool validateLanguageFiles();

    // Utility

    /**
     * @brief Check if language manager is ready
     *
     * @return true if initialized, false otherwise
     */
    bool isReady() const { return _ready; }

    /**
     * @brief Print debug information about language system
     */
    void printDebugInfo();

private:
    /**
     * @brief Load string mappings from JSON file
     *
     * @param lang_code Language code
     * @return true on success, false on failure
     */
    bool loadStringsFromFile(const char* lang_code);

    // String storage
    std::map<String, String> _strings;
    String _current_language;

    // State
    bool _ready;

    // Filesystem reference
    LittleFSManager* _fs;

    // Available languages
    static const char* AVAILABLE_LANGUAGES[];
    static const size_t LANGUAGE_COUNT;
};
