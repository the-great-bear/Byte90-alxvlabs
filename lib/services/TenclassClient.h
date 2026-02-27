/**
 * TenclassClient.h
 *
 * Declarations for TenclassClient.
 */

#pragma once

// Project includes
#include "SecureHttpClient.h" // Use SecureHttpClient wrapper

// Arduino/ESP32 includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_hmac.h>
#include <esp_ota_ops.h>

// Standard includes
#include <sys/time.h>

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;

/**
 * Protocol configuration structure
 *
 * Contains connection parameters for various communication protocols
 * received from the provisioning server.
 */
struct ProtocolConfig {
    // WebSocket configuration
    bool hasWebSocket = false;
    String wsHost;
    int wsPort = 0;
    String wsPath;
    bool wsUseSSL = false;
    String wsToken;
    int wsVersion = 1;

    // MQTT configuration
    bool hasMqtt = false;
    String mqttBroker;
    int mqttPort = 0;
    String mqttClientId;
    String mqttUsername;
    String mqttPassword;
    String mqttTopicPrefix;

    // UDP configuration
    String udpHost;
    int udpPort = 0;

    // Version information
    String firmwareVersion;
    String assetsVersion;
};

/**
 * TenclassClient - Device provisioning and configuration client
 *
 * Features:
 * - HTTP(S) provisioning server communication
 * - Device activation flow
 * - Protocol configuration retrieval
 * - Server time synchronization
 * - eFuse serial number reading
 *
 * Architecture:
 * - Contacts provisioning server with device info
 * - Receives protocol configuration (WebSocket, MQTT, UDP)
 * - Handles device activation challenge/response
 * - Syncs device time with server
 *
 * Activation flow:
 * 1. Send device info to server
 * 2. Receive activation challenge
 * 3. Generate HMAC response
 * 4. Receive activation code and config
 */
class TenclassClient {
public:
    /**
     * @brief Construct provisioning client instance
     *
     * @param url Provisioning server URL (http:// or https://)
     * @param boardType Board type identifier (e.g., "xiao-esp32s3")
     * @param storage Pointer to NVSStorage instance for UUID access
     */
    TenclassClient(const String& url, const String& boardType, NVSStorage* storage);

    /**
     * @brief Check configuration from provisioning server
     *
     * Contacts server and retrieves protocol configuration.
     *
     * @param config Output: protocol configuration structure
     * @return true on success, false on failure
     */
    bool checkConfig(ProtocolConfig& config);

    /**
     * @brief Load cached configuration from NVS (if available)
     *
     * @param config Output: protocol configuration structure
     * @return true if cached config was loaded, false otherwise
     */
    bool loadCachedConfig(ProtocolConfig& config);

    /**
     * @brief Get last error message
     *
     * @return Error message string
     */
    String getLastError() const { return _lastError; }

    /**
     * @brief Perform device activation
     *
     * Completes activation challenge/response flow with server.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t Activate();

    /**
     * @brief Check if activation challenge was received
     *
     * @return true if challenge present, false otherwise
     */
    bool HasActivationChallenge() const { return _hasActivationChallenge; }

    /**
     * @brief Check if activation code was received
     *
     * @return true if code present, false otherwise
     */
    bool HasActivationCode() const { return _hasActivationCode; }

    /**
     * @brief Check if server time was received
     *
     * @return true if time synced, false otherwise
     */
    bool HasServerTime() const { return _hasServerTime; }

    /**
     * @brief Get activation message
     *
     * @return Activation message string
     */
    String GetActivationMessage() const { return _activationMessage; }

    /**
     * @brief Get activation code
     *
     * @return Activation code string
     */
    String GetActivationCode() const { return _activationCode; }

private:
    /**
     * @brief Make HTTP(S) request to provisioning server
     *
     * @param response Output: server response body
     * @return true on success, false on failure
     */
    bool makeRequest(String& response);

    /**
     * @brief Parse provisioning server response
     *
     * @param response JSON response string from server
     * @param config Output: parsed protocol configuration
     * @return true on success, false on failure
     */
    bool parseResponse(const String& response, ProtocolConfig& config);

    /**
     * @brief Parse activation section from server response
     *
     * @param activation JSON object containing activation data
     * @return true on success, false on failure
     */
    bool parseActivation(const JsonObject& activation);

    /**
     * @brief Parse server time section from response
     *
     * @param serverTime JSON object containing server timestamp
     * @return true on success, false on failure
     */
    bool parseServerTime(const JsonObject& serverTime);

    /**
     * @brief Store protocol configuration to NVS
     *
     * @param config Protocol configuration to store
     * @return true on success, false on failure
     */
    bool storeConfig(const ProtocolConfig& config);

    /**
     * @brief Generate activation payload JSON
     *
     * @return JSON string with device info and activation response
     */
    String getActivationPayload();

    /**
     * @brief Read device serial number from eFuse
     *
     * @return true on success, false on failure
     */
    bool readSerialNumber();

    // Storage
    NVSStorage* _storage;

    // Configuration
    String _url;
    String _boardType;
    String _lastError;

    // Parsed URL components
    String _host;
    int _port;
    String _path;
    bool _useSSL;

    // Activation data
    String _activationMessage;
    String _activationCode;
    String _activationChallenge;
    bool _hasActivationCode;
    bool _hasActivationChallenge;
    uint32_t _activationTimeoutMs;

    // Serial number (from eFuse)
    String _serialNumber;
    bool _hasSerialNumber;

    // Server time sync
    bool _hasServerTime;
};
