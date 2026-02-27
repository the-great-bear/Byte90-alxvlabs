/**
 * MQTTClient.h
 *
 * Declarations for MQTTClient.
 */

#pragma once

// Arduino/ESP32 includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <mbedtls/aes.h>

// Project includes
#include "AudioService.h"     // For AudioStreamPacket
#include "TenclassClient.h"   // For ProtocolConfig
#include "DeviceConfig.h"
/**
 * MQTTClient - MQTT + UDP protocol implementation
 *
 * Features:
 * - MQTT control messages (hello/goodbye + JSON)
 * - UDP audio streaming with AES-CTR encryption
 * - Automatic reconnection
 * - Callback-based event handling
 */
class MQTTClient {
public:
    MQTTClient();
    ~MQTTClient();

    // Lifecycle
    bool start();
    bool configure(const struct ProtocolConfig& config);
    bool openAudioChannel();
    void closeAudioChannel();
    bool isAudioChannelOpened() const;

    // Message sending
    bool sendMessage(const String& message);
    bool sendAudio(AudioStreamPacket* packet);

    // Connection status
    bool isConnected() const;

    // Polling
    void poll();

    // Callback registration
    void onIncomingJson(std::function<void(const cJSON* root)> callback);
    void onIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback);
    void onAudioChannelOpened(std::function<void()> callback);
    void onAudioChannelClosed(std::function<void()> callback);
    void onNetworkError(std::function<void(const String& message)> callback);
    void onConnected(std::function<void()> callback);
    void onDisconnected(std::function<void()> callback);
    void onReconnected(std::function<void()> callback);

    /**
     * @brief Disconnect MQTT and close audio channel
     */
    void disconnect();

    /**
     * @brief Check if hello was received
     *
     * @return true if received, false otherwise
     */
    bool isHelloReceived() const { return _hello_received; }

    /**
     * @brief Get current session ID
     *
     * @return Session identifier string
     */
    const String& sessionId() const { return _session_id; }

    /**
     * @brief Get server-negotiated sample rate
     *
     * @return Sample rate in Hz
     */
    int serverSampleRate() const { return _server_sample_rate; }

    /**
     * @brief Get server-negotiated frame duration
     *
     * @return Frame duration in milliseconds
     */
    int serverFrameDuration() const { return _server_frame_duration; }

private:
    // MQTT handling
    void setupMqttClient();
    bool connectMqtt();
    void disconnectMqtt();
    void handleMqttMessage(int message_size);
    static void staticMqttCallback(int message_size);

    // Hello/handshake
    bool publishHello();
    void parseServerHello(const JsonDocument& doc);
    void parseServerHelloCommon(const JsonDocument& doc);
    String buildHelloMessage();

    // UDP handling
    bool connectUdp(const String& server, int port);
    void disconnectUdp();
    bool isUdpConnected() const;
    int sendUdp(const uint8_t* data, size_t len);
    void pollUdp();
    void handleUdpPacket(const uint8_t* data, size_t len);

    // AES encryption
    void initializeAES(const String& hex_key, const String& hex_nonce);
    String decodeHexString(const String& hex_string);

    // ArduinoMqttClient (direct)
    WiFiClientSecure* _secure_client;
    WiFiClient* _client;
    MqttClient* _mqtt_client;

    // WiFiUDP (direct)
    WiFiUDP _udp;
    String _udp_server;
    int _udp_port;
    bool _udp_connected;
    static const size_t UDP_MAX_PACKET_SIZE = 2048;
    uint8_t _udp_buffer[UDP_MAX_PACKET_SIZE];

    // AES encryption
    mbedtls_aes_context _aes_ctx;
    uint8_t _aes_nonce[16];
    bool _aes_initialized;

    // Sequence tracking
    uint32_t _local_sequence;
    uint32_t _remote_sequence;

    // State
    bool _error_occurred;
    bool _mqtt_connected;
    bool _hello_received;
    String _session_id;
    int _server_sample_rate;
    int _server_frame_duration;

    // MQTT configuration
    String _broker;
    int _port;
    bool _use_ssl;
    String _client_id;
    String _username;
    String _password;
    String _publish_topic;
    String _subscribe_topic;

    // UDP configuration (from server hello)
    String _udp_key;
    String _udp_nonce;

    // Reconnection
    bool _auto_reconnect;
    BackoffState _backoff;
    uint32_t _max_reconnect_attempts;
    bool _reconnect_exhausted;
    bool _intentional_disconnect;
    SemaphoreHandle_t _connect_mutex;
    bool _connect_in_progress;

    // Callbacks
    std::function<void(const cJSON* root)> _on_incoming_json;
    std::function<void(AudioStreamPacket* packet)> _on_incoming_audio;
    std::function<void()> _on_audio_channel_opened;
    std::function<void()> _on_audio_channel_closed;
    std::function<void(const String& message)> _on_network_error;
    std::function<void()> _on_connected;
    std::function<void()> _on_disconnected;
    std::function<void()> _on_reconnected;

    // Static instance for ArduinoMqttClient callback
    static MQTTClient* instance;

};
