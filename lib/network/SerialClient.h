/**
 * SerialClient.h
 *
 * Declarations for SerialClient.
 */

#pragma once

// Project includes
#include "WifiManager.h"

// System includes
#include <Arduino.h>
#include <Update.h>

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;

// Constants

/**
 * Serial command identifiers for device control via UART
 */
#define CMD_WIFI_SCAN "WIFI_SCAN"
#define CMD_WIFI_STATUS "WIFI_STATUS"
#define CMD_WIFI_CONNECT "WIFI_CONNECT"
#define CMD_WIFI_DISCONNECT "WIFI_DISCONNECT"
#define CMD_WIFI_GET_SAVED "WIFI_GET_SAVED"
#define CMD_WIFI_FORGET "WIFI_FORGET"
#define CMD_GET_INFO "GET_INFO"
#define CMD_RESTART "RESTART"
#define CMD_GET_STATUS "GET_STATUS"
#define CMD_START_UPDATE "START_UPDATE"
#define CMD_SEND_CHUNK "SEND_CHUNK"
#define CMD_FINISH_UPDATE "FINISH_UPDATE"
#define CMD_ABORT_UPDATE "ABORT_UPDATE"
#define CMD_GET_LOGS "GET_LOGS"
#define CMD_VERBOSE "VERBOSE"

/**
 * Serial response prefixes
 */
#define RESP_OK "OK:"
#define RESP_ERROR "ERROR:"
#define RESP_PROGRESS "PROGRESS:"

/**
 * Serial update settings
 */
#define SERIAL_BAUD_RATE 921600
#define SERIAL_COMMAND_BUFFER_SIZE 4096

/**
 * SerialClient - Serial command interface for device configuration
 *
 * Features:
 * - WiFi management via Serial commands
 * - Device information reporting
 * - JSON-formatted responses
 * - Command buffering and parsing
 *
 * Supported commands:
 * - WiFi: SCAN, STATUS, CONNECT, DISCONNECT, GET_SAVED, FORGET
 * - Device: GET_INFO, GET_STATUS, RESTART
 *
 * Architecture:
 * - Reads commands from Serial (UART)
 * - Parses and validates command format
 * - Executes command and returns JSON response
 * - Used for device provisioning and debugging
 */
class SerialClient {
public:
    /**
     * @brief Construct serial client instance
     *
     * @param wifiClient Pointer to WiFi manager
     * @param storage Pointer to NVS storage
     */
    SerialClient(WifiManager* wifiClient, NVSStorage* storage);

    /**
     * @brief Initialize serial client
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Process incoming serial commands
     *
     * Call regularly from main loop to handle serial input.
     */
    void loop();

    /**
     * @brief Enable or disable verbose logging
     *
     * @param enabled true to enable verbose output, false to disable
     */
    void setVerbose(bool enabled) { _verbose = enabled; }


private:
    enum class SerialUpdateState {
        IDLE,
        RECEIVING,
        PROCESSING,
        SUCCESS,
        ERROR
    };

    struct UpdateProgress {
        size_t totalSize;
        size_t receivedSize;
        int percentage;
        String message;
    };

    // Command processing

    /**
     * @brief Parse and execute serial command
     *
     * @param line Command line string
     */
    void processCommand(const String& line);

    // Command handlers

    /**
     * @brief Handle WiFi scan command
     */
    void handleWiFiScan();

    /**
     * @brief Handle WiFi status command
     */
    void handleWiFiStatus();

    /**
     * @brief Handle WiFi connect command
     *
     * @param data JSON data with SSID and password
     */
    void handleWiFiConnect(const String& data);

    /**
     * @brief Handle WiFi disconnect command
     */
    void handleWiFiDisconnect();

    /**
     * @brief Handle get saved WiFi credentials command
     */
    void handleWiFiGetSaved();

    /**
     * @brief Handle forget WiFi credentials command
     */
    void handleWiFiForget();

    /**
     * @brief Handle get device info command
     */
    void handleGetInfo();

    /**
     * @brief Handle get device status command
     */
    void handleGetStatus();

    /**
     * @brief Handle restart device command
     */
    void handleRestart();

    /**
     * @brief Handle start update command
     */
    void handleStartUpdate(const String& data);

    /**
     * @brief Handle send chunk command
     */
    void handleSendChunk(const String& data);

    /**
     * @brief Handle finish update command
     */
    void handleFinishUpdate();

    /**
     * @brief Handle abort update command
     */
    void handleAbortUpdate();

    // Response helpers

    /**
     * @brief Send JSON response over serial
     *
     * @param jsonResponse JSON response string
     * @param isError true if error response, false for success
     */
    void sendResponse(const String& jsonResponse, bool isError = false);

    /**
     * @brief Create JSON response for WiFi operations
     *
     * @param success true if operation succeeded
     * @param message Response message
     * @param ssid WiFi SSID
     * @param rssi Signal strength
     * @param connected Connection status
     * @param networks JSON array of scanned networks
     * @return JSON response string
     */
    String createJsonResponse(bool success, const String& message,
                             const String& ssid = "", int rssi = 0,
                             bool connected = false, const String& networks = "[]");

    /**
     * @brief Create device information JSON response
     *
     * @return JSON string with device info
     */
    String createDeviceInfoResponse();

    /**
     * @brief Create WiFi credentials JSON response
     *
     * @param success true if operation succeeded
     * @param message Response message
     * @param ssid Saved WiFi SSID
     * @param hasCredentials true if credentials exist in NVS
     * @return JSON response string
     */
    String createWiFiCredentialsResponse(bool success, const String& message,
                                        const String& ssid = "", bool hasCredentials = false);

    /**
     * @brief Create JSON response for serial update status/progress
     */
    String createSerialUpdateResponse(bool success, const String& message,
                                     bool completed = false, int progress = 0);

    /**
     * @brief Send progress update with PROGRESS prefix
     */
    void sendProgressUpdate(int percentage, const String& message);

    /**
     * @brief Decode base64 chunk into output buffer
     */
    size_t simpleBase64Decode(const String& input, uint8_t* output, size_t maxOutputSize);

    /**
     * @brief Get string form of update state
     */
    const char* getSerialStateString() const;

    /**
     * @brief Initialize update with size/type
     */
    bool initializeSerialUpdate(const String& data);

    /**
     * @brief Write one decoded chunk
     */
    int handleChunkWrite(const String& data);

    /**
     * @brief Check if serial update is active
     */
    bool isSerialUpdateActive() const { return _update_state != SerialUpdateState::IDLE; }

    /**
     * @brief Convert WiFi security type enum to string
     *
     * @param encType WiFi authentication mode
     * @return Security type string (e.g., "WPA2", "OPEN")
     */
    String getSecurityType(wifi_auth_mode_t encType);

    /**
     * @brief Convert RSSI to signal strength description
     *
     * @param rssi Signal strength in dBm
     * @return Signal strength string (e.g., "Excellent", "Good")
     */
    String getSignalStrength(int rssi);

    // Dependencies
    WifiManager* _wifiClient;
    NVSStorage* _storage;

    // State
    String _commandBuffer;
    bool _verbose;
    SerialUpdateState _update_state;
    UpdateProgress _update_progress;
    size_t _expected_firmware_size;
    int _current_update_command;
    size_t _total_written;
};
