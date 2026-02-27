/**
 * ApiClient.h
 *
 * Declarations for ApiClient.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Project includes
#include "DeviceConfig.h"

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;
/**
 * @brief ProtocolClient.
 */
class ProtocolClient;

/**
 * @brief ApiClient.
 */
class ApiClient {
public:
    ApiClient(ProtocolClient* protocol, NVSStorage* storage);

    /**
     * @brief Send listen start command
     * @param sessionId Current session ID
     * @param autoMode true for "auto", false for "manual"
     * @return true if sent successfully
     */
    bool sendListenStart(const String& sessionId, bool autoMode);

    /**
     * @brief Send listen detect command
     * @param sessionId Current session ID
     * @param text Detected text or trigger label
     * @return true if sent successfully
     */
    bool sendListenDetect(const String& sessionId, const String& text);

    /**
     * @brief Send listen stop command
     * @param sessionId Current session ID
     * @return true if sent successfully
     */
    bool sendListenStop(const String& sessionId);

    /**
     * @brief Send abort command
     * @param sessionId Current session ID
     * @param reason Reason for aborting
     * @return true if sent successfully
     */
    bool sendAbort(const String& sessionId, const String& reason);

    /**
     * @brief Send MCP response
     * @param sessionId Current session ID
     * @param payload The MCP response payload
     * @return true if sent successfully
     */
    bool sendMcpResponse(const String& sessionId, const JsonObject& payload);

    /**
     * @brief Generate activation payload for device provisioning
     * @param serialNumber Device serial number (empty for V1 activation)
     * @param challenge Activation challenge string (empty for V1 activation)
     * @return JSON payload string
     */
    static String generateActivationPayload(const String& serialNumber, const String& challenge);

    /**
     * @brief Build device information JSON for provisioning
     * @param storage NVS storage instance for UUID access
     * @return JSON payload string
     */
    static String buildDeviceInfo(NVSStorage* storage);

private:
    ProtocolClient* _protocol;
    NVSStorage* _storage;

    bool sendJson(const JsonDocument& doc);
};
