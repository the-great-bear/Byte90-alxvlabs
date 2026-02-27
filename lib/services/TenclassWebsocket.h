/**
 * TenclassWebsocket.h
 *
 * Declarations for TenclassWebsocket.
 */

#pragma once

// Project includes
#include "AudioService.h"        // For AudioStreamPacket
#include "TenclassClient.h" // For ProtocolConfig
#include "WebsocketClient.h"

// Arduino/ESP32 includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cJSON.h>

// Standard includes
#include <functional>

// Forward declarations
/**
 * @brief NVSStorage.
 */
class NVSStorage;


// Type definitions

/**
 * @brief __attribute__.
 * Binary protocol structure for WebSocket v2
 *
 * Packed structure for efficient binary message transmission.
 */
struct __attribute__((packed)) BinaryProtocol2 {
    uint16_t version;      // Protocol version
    uint16_t type;         // Message type
    uint32_t reserved;     // Reserved for future use
    uint32_t timestamp;    // Message timestamp
    uint32_t payload_size; // Payload size in bytes
    uint8_t payload[];     // Variable-length payload data
};

/**
 * TenclassWebsocket - WebSocket-based communication protocol implementation
 *
 * Features:
 * - WebSocket connection management
 * - JSON message handling
 * - Binary audio streaming
 * - Protocol version 1 and 2 support
 * - Automatic reconnection
 * - Callback registration and management
 * - Session state management
 *
 * Architecture:
 * - Uses WebSocketClient for transport
 * - Supports hello handshake for session establishment
 * - Binary protocol for efficient audio transmission
 * - Callbacks decouple protocol from application logic
 *
 * Protocol flow:
 * 1. Connect to WebSocket server
 * 2. Send hello message
 * 3. Receive server hello response
 * 4. Open audio channel
 * 5. Stream audio + JSON messages
 */
class TenclassWebsocket {
public:
    /**
     * @brief Construct WebSocket protocol instance
     *
     * @param storage Pointer to NVSStorage instance for UUID access
     */
    TenclassWebsocket(NVSStorage* storage);

    /**
     * @brief Destroy WebSocket protocol and cleanup resources
     */
    ~TenclassWebsocket();

    // Lifecycle

    /**
     * @brief Start WebSocket protocol operation
     *
     * @return true on success, false on failure
     */
    bool Start();

    /**
     * @brief Configure WebSocket protocol with connection parameters
     *
     * @param config Protocol configuration with WebSocket host/port/path
     * @return true on success, false on failure
     */
    bool Configure(const struct ProtocolConfig& config);

    /**
     * @brief Open audio channel (sends hello and waits for response)
     *
     * @return true on success, false on failure
     */
    bool OpenAudioChannel();

    /**
     * @brief Close audio channel
     */
    void CloseAudioChannel();

    /**
     * @brief Check if audio channel is currently open
     *
     * @return true if open, false otherwise
     */
    bool IsAudioChannelOpened() const;

    // Message sending

    /**
     * @brief Send JSON text message over WebSocket
     *
     * @param message JSON message string
     * @return true on success, false on failure
     */
    bool sendMessage(const String& message);

    /**
     * @brief Send audio packet over WebSocket
     *
     * @param packet Audio stream packet to send
     * @return true on success, false on failure
     */
    bool SendAudio(AudioStreamPacket* packet);

    // Connection status

    /**
     * @brief Check if WebSocket is connected
     *
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    // Polling

    /**
     * @brief Poll WebSocket for incoming messages
     */
    void poll();

    // Callback registration

    /**
     * @brief Register callback for incoming JSON messages
     *
     * @param callback Function called with parsed cJSON root object
     */
    void OnIncomingJson(std::function<void(const cJSON* root)> callback);

    /**
     * @brief Register callback for incoming audio packets
     *
     * @param callback Function called with received audio packet
     */
    void OnIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback);

    /**
     * @brief Register callback for audio channel opened event
     *
     * @param callback Function called when audio channel opens
     */
    void OnAudioChannelOpened(std::function<void()> callback);

    /**
     * @brief Register callback for audio channel closed event
     *
     * @param callback Function called when audio channel closes
     */
    void OnAudioChannelClosed(std::function<void()> callback);

    /**
     * @brief Register callback for network errors
     *
     * @param callback Function called with error message
     */
    void OnNetworkError(std::function<void(const String& message)> callback);

    /**
     * @brief Register callback for connection established event
     *
     * @param callback Function called when connected
     */
    void OnConnected(std::function<void()> callback);

    /**
     * @brief Register callback for disconnection event
     *
     * @param callback Function called when disconnected
     */
    void OnDisconnected(std::function<void()> callback);

    // Compatibility wrappers

    /**
     * @brief Send text message (compatibility wrapper)
     *
     * @param message Text message to send
     * @return true on success, false on failure
     */
    bool sendTXT(String& message) {
        String temp = message;
        return sendMessage(temp);
    }

    /**
     * @brief Send binary data (compatibility wrapper)
     *
     * @param data Binary data buffer
     * @param len Length of data in bytes
     * @return true on success, false on failure
     */
    bool sendBIN(const uint8_t* data, size_t len);

    // Accessors

    /**
     * @brief Get protocol version
     *
     * @return Protocol version number (1 or 2)
     */
    int protocol_version() const { return protocol_version_; }

    /**
     * @brief Check if server hello was received
     *
     * @return true if received, false otherwise
     */
    bool IsHelloReceived() const { return hello_received_; }

    /**
     * @brief Get WebSocket client instance
     *
     * @return Pointer to WebSocketClient
     */
    WebSocketClient* getClient() const { return wsClient_; }

    /**
     * @brief Get WebSocket client instance (alias)
     *
     * @return Pointer to WebSocketClient
     */
    WebSocketClient* getWsClient() const { return wsClient_; }

    /**
     * @brief Get server-negotiated sample rate
     *
     * @return Sample rate in Hz
     */
    int server_sample_rate() const { return server_sample_rate_; }

    /**
     * @brief Get server-negotiated frame duration
     *
     * @return Frame duration in milliseconds
     */
    int server_frame_duration() const { return server_frame_duration_; }

    /**
     * @brief Get current session ID
     *
     * @return Session identifier string
     */
    const String& session_id() const { return session_id_; }

private:
    // WebSocket connection

    /**
     * @brief Initialize WebSocket client with configuration
     */
    void setupWebSocket();

    /**
     * @brief Handle WebSocket events (connected, disconnected, data)
     *
     * @param type WebSocket event type
     * @param payload Event payload data
     * @param length Payload length in bytes
     */
    void handleWebSocketEvent(WSTYPE_t type, uint8_t* payload, size_t length);

    // Protocol handshake

    /**
     * @brief Send hello message to server
     *
     * @return true on success, false on failure
     */
    bool sendHello();

    /**
     * @brief Parse server hello response
     *
     * @param doc Parsed JSON document containing server hello
     */
    void parseServerHello(const JsonDocument& doc);

    // Binary protocol handling

    /**
     * @brief Handle incoming binary packet
     *
     * @param payload Binary packet data
     * @param length Packet length in bytes
     */
    void handleBinaryPacket(const uint8_t* payload, size_t length);

    // WebSocket client
    WebSocketClient* wsClient_;

    // Storage
    NVSStorage* _storage;

    // Configuration
    ProtocolConfig config_;

    // Session state
    String session_id_;
    int server_sample_rate_;
    int server_frame_duration_;
    bool hello_received_;

    // WebSocket state
    int protocol_version_;
    bool hello_sent_;
    bool audio_channel_opened_;
    bool intentional_disconnect_;

    // Callback functions
    std::function<void(const cJSON* root)> on_incoming_json_;
    std::function<void(AudioStreamPacket* packet)> on_incoming_audio_;
    std::function<void()> on_audio_channel_opened_;
    std::function<void()> on_audio_channel_closed_;
    std::function<void(const String& message)> on_network_error_;
    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
};
