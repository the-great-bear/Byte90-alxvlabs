/**
 * WebsocketClient.h
 *
 * Declarations for WebsocketClient.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "DeviceConfig.h"

/**
 * WebSocket event types
 */
enum WSTYPE_t {
    WSTYPE_ERROR,        // WebSocket error occurred
    WSTYPE_DISCONNECTED, // WebSocket disconnected
    WSTYPE_CONNECTED,    // WebSocket connected
    WSTYPE_TEXT,         // Text message received
    WSTYPE_BIN,          // Binary message received
    WSTYPE_PING,         // Ping frame received
    WSTYPE_PONG,         // Pong frame received
};

/**
 * WebSocket frame opcodes (RFC 6455)
 */
enum WSOPcode_t {
    WSOP_continuation = 0x00, // Continuation frame
    WSOP_text         = 0x01, // Text frame
    WSOP_binary       = 0x02, // Binary frame
    WSOP_close        = 0x08, // Close frame
    WSOP_ping         = 0x09, // Ping frame
    WSOP_pong         = 0x0A  // Pong frame
};

/**
 * WebSocketClient - WebSocket client implementation
 *
 * Features:
 * - WebSocket (RFC 6455) client for ESP32
 * - Non-blocking operation
 * - SSL/TLS support
 * - Automatic reconnection
 * - Text and binary messages
 * - Ping/pong heartbeat
 *
 * Architecture:
 * - Event-driven callback model
 * - State machine for connection handling
 * - Frame parsing and encoding
 * - Masking for client-to-server frames
 *
 * Usage:
 * 1. Create instance
 * 2. Register event callback with onEvent()
 * 3. Call begin() or beginSSL()
 * 4. Call loop() regularly
 */
class WebSocketClient {
public:
    /**
 * @brief void.
     * Event callback function type
     *
     * @param type Event type (connected, disconnected, text, binary, etc.)
     * @param payload Pointer to message data (for TEXT/BIN events)
     * @param length Length of payload in bytes
     */
    typedef std::function<void(WSTYPE_t type, uint8_t* payload, size_t length)> EventCallback;

    /**
     * @brief Construct WebSocket client instance
     */
    WebSocketClient();

    /**
     * @brief Destroy WebSocket client and cleanup resources
     */
    ~WebSocketClient();

    /**
     * @brief Begin WebSocket connection (unencrypted)
     *
     * @param host Server hostname or IP address
     * @param port Server port (typically 80)
     * @param url WebSocket endpoint path (default "/")
     * @param protocol WebSocket subprotocol (default "arduino")
     */
    void begin(const char* host, uint16_t port, const char* url = "/", const char* protocol = "arduino");

    /**
     * @brief Begin WebSocket connection with SSL/TLS
     *
     * @param host Server hostname or IP address
     * @param port Server port (typically 443)
     * @param url WebSocket endpoint path (default "/")
     * @param certificate SSL certificate (PEM format) for validation (if nullptr, uses setInsecure)
     * @param protocol WebSocket subprotocol (default "arduino")
     */
    void beginSSL(const char* host, uint16_t port, const char* url = "/", const char* certificate = nullptr, const char* protocol = "arduino");

    /**
     * @brief Process WebSocket events and data
     *
     * Call regularly from main loop to handle connection and messages.
     */
    void loop();

    /**
     * @brief Disconnect WebSocket connection
     */
    void disconnect();

    /**
     * @brief Send text message
     *
     * @param payload Text data (null-terminated string)
     * @param length Optional length (0 = auto-calculate from null terminator)
     * @return true on success, false on failure
     */
    bool sendTXT(const char* payload, size_t length = 0);

    /**
     * @brief Send text message
     *
     * @param payload Text message as String
     * @return true on success, false on failure
     */
    bool sendTXT(const String& payload);

    /**
     * @brief Send binary message
     *
     * @param payload Binary data buffer
     * @param length Length of data in bytes
     * @return true on success, false on failure
     */
    bool sendBIN(const uint8_t* payload, size_t length);

    /**
     * @brief Send ping frame
     *
     * @param payload Optional ping data
     * @param length Length of ping data
     * @return true on success, false on failure
     */
    bool sendPing(uint8_t* payload = NULL, size_t length = 0);

    /**
     * @brief Register event callback
     *
     * @param callback Function to call for WebSocket events
     */
    void onEvent(EventCallback callback);

    /**
     * @brief Set extra HTTP headers for handshake
     *
     * @param headers Extra headers string (e.g., "Authorization: Bearer token\r\n")
     */
    void setExtraHeaders(const char* headers);

    /**
     * @brief Set reconnection interval
     *
     * @param time Interval in milliseconds between reconnection attempts
     */
    void setReconnectInterval(unsigned long time);
    void setMaxReconnectAttempts(uint32_t max_attempts);

    /**
     * @brief Check if WebSocket is connected
     *
     * @return true if connected, false otherwise
     */
    bool isConnected();
    bool isHealthy() const { return _socket_healthy; }

private:
    // Connection state enumeration
    enum ClientStatus {
        STATUS_NOT_CONNECTED, // Not connected
        STATUS_HEADER,        // Reading HTTP handshake response
        STATUS_CONNECTED      // WebSocket established
    };

    /**
     * @brief Establish TCP/TLS connection and perform WebSocket handshake
     *
     * @return true on success, false on failure
     */
    bool connect();

    /**
     * @brief Attempt to establish TCP/SSL connection
     */
    void attemptConnection();

    /**
     * @brief Handle incoming WebSocket frames
     */
    void handleData();

    /**
     * @brief Send WebSocket frame
     *
     * @param opcode Frame opcode (text, binary, ping, pong, etc.)
     * @param payload Frame payload data
     * @param length Payload length in bytes
     * @param mask true to mask payload (required for client-to-server)
     * @param fin true for final fragment
     * @return true on success, false on failure
     */
    bool sendFrame(WSOPcode_t opcode, const uint8_t* payload, size_t length, bool mask = false, bool fin = true);

    /**
     * @brief Generate random WebSocket handshake key
     *
     * @return Base64-encoded random key
     */
    String generateKey();

    /**
     * @brief Compute expected Sec-WebSocket-Accept value
     *
     * @param clientKey Client's Sec-WebSocket-Key value
     * @return Expected server Sec-WebSocket-Accept value
     */
    String acceptKey(const String& clientKey);

    /**
     * @brief Invoke registered event callback
     *
     * @param type Event type
     * @param payload Event payload data
     * @param length Payload length
     */
    void runCallback(WSTYPE_t type, uint8_t* payload, size_t length);

    /**
     * @brief Handle heartbeat ping sending
     */
    void handleHeartbeatPing();

    /**
     * @brief Handle heartbeat pong timeout
     */
    void handleHeartbeatTimeout();
    void logSslError(const char* context);
    bool lockWs(const char* context);
    void unlockWs();

    // Network clients
    WiFiClient* _tcp;
    WiFiClientSecure* _ssl;

    // Connection configuration
    String _host;
    uint16_t _port;
    String _url;
    String _protocol;
    String _extra_headers;
    const char* _certificate;

    // Connection state
    bool _is_connected;
    bool _is_ssl;
    BackoffState _backoff;
    uint32_t _max_reconnect_attempts;
    bool _reconnect_exhausted;
    ClientStatus _status;

    // Event callback
    EventCallback _event_callback;

    // WebSocket frame parsing state
    uint8_t _ws_header[14];
    uint32_t _ws_header_size;
    uint32_t _ws_payload_size;
    uint8_t* _ws_payload;
    uint8_t _ws_opcode;
    bool _ws_fin;
    size_t _ws_bytes_read;
    size_t _ws_discard_remaining;
    uint8_t* _ws_message;
    size_t _ws_message_size;
    size_t _ws_message_capacity;
    uint8_t _ws_message_opcode;

    // Heartbeat variables
    uint32_t _ping_interval;
    uint32_t _pong_timeout;
    uint8_t _disconnect_timeout_count;
    uint8_t _pong_timeout_count;
    uint32_t _last_ping;
    bool _pong_received;
    bool _disconnecting;
    SemaphoreHandle_t _ws_mutex;
    bool _socket_healthy;
};
