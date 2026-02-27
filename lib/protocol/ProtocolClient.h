/**
 * ProtocolClient.h
 *
 * Declarations for ProtocolClient.
 */

#pragma once

#include <Arduino.h>

/**
 * ProtocolClient - Abstract protocol interface for control-plane messaging.
 */
class ProtocolClient {
public:
    virtual ~ProtocolClient() = default;

    /**
     * @brief Check if protocol is connected
     *
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Check if audio channel is open
     *
     * @return true if audio channel is open, false otherwise
     */
    virtual bool isAudioChannelOpened() const = 0;

    /**
     * @brief Send a JSON message over the protocol
     *
     * @param message Serialized JSON payload
     * @return true if sent successfully
     */
    virtual bool sendMessage(const String& message) = 0;
};
