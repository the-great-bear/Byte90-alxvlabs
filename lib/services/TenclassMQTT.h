/**
 * TenclassMQTT.h
 *
 * Declarations for TenclassMQTT.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <cJSON.h>
#include <functional>

// Project includes
#include "AudioService.h"
#include "../network/MQTTClient.h"
#include "TenclassClient.h"

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;

/**
 * TenclassMQTT - MQTT-based communication protocol implementation
 *
 * Features:
 * - MQTT connection management
 * - JSON message handling
 * - UDP audio streaming
 * - Automatic reconnection
 * - Callback registration and management
 * - Session state management
 */
class TenclassMQTT {
public:
    /**
     * @brief Construct MQTT protocol instance
     *
     * @param storage Pointer to NVSStorage instance (reserved for future use)
     */
    TenclassMQTT(NVSStorage* storage);

    /**
     * @brief Destroy MQTT protocol and cleanup resources
     */
    ~TenclassMQTT();

    // Lifecycle
    bool Start();
    bool Configure(const struct ProtocolConfig& config);
    bool OpenAudioChannel();
    void CloseAudioChannel();
    bool IsAudioChannelOpened() const;

    // Message sending
    bool sendMessage(const String& message);
    bool SendAudio(AudioStreamPacket* packet);

    // Connection status
    bool isConnected() const;

    // Polling
    void poll();

    // Callback registration
    void OnIncomingJson(std::function<void(const cJSON* root)> callback);
    void OnIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback);
    void OnAudioChannelOpened(std::function<void()> callback);
    void OnAudioChannelClosed(std::function<void()> callback);
    void OnNetworkError(std::function<void(const String& message)> callback);
    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);
    void OnReconnected(std::function<void()> callback);

    /**
     * @brief Get MQTT client instance
     *
     * @return Pointer to MQTTClient
     */
    MQTTClient* getClient() const { return _mqtt_client; }

    /**
     * @brief Get server-negotiated sample rate
     *
     * @return Sample rate in Hz
     */
    int server_sample_rate() const;

    /**
     * @brief Get server-negotiated frame duration
     *
     * @return Frame duration in milliseconds
     */
    int server_frame_duration() const;

    /**
     * @brief Check if hello was received
     *
     * @return true if received, false otherwise
     */
    bool IsHelloReceived() const;

    /**
     * @brief Get current session ID
     *
     * @return Session identifier string
     */
    const String& session_id() const;

private:
    // Owned MQTT client
    MQTTClient* _mqtt_client;

    // References
    NVSStorage* _storage;
};
