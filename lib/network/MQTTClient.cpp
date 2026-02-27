/**
 * MQTTClient.cpp
 *
 * Implementation for MQTTClient.
 */

#include "MQTTClient.h"

#include "DeviceConfig.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

#include <arpa/inet.h>

static const char* TAG = "MQTTClient";
static const unsigned long CONNECT_TIMEOUT_MS = 10000;
static const int MAX_CONNECT_RETRIES = 2;

// Static instance for ArduinoMqttClient callback
MQTTClient* MQTTClient::instance = nullptr;

MQTTClient::MQTTClient()
    : _secure_client(nullptr)
    , _client(nullptr)
    , _mqtt_client(nullptr)
    , _udp_port(0)
    , _udp_connected(false)
    , _aes_initialized(false)
    , _local_sequence(0)
    , _remote_sequence(0)
    , _error_occurred(false)
    , _mqtt_connected(false)
    , _hello_received(false)
    , _session_id("")
    , _server_sample_rate(AUDIO_SAMPLE_RATE_STT)
    , _server_frame_duration(AUDIO_OPUS_FRAME_MS)
    , _port(1883)
    , _use_ssl(false)
    , _auto_reconnect(true)
    , _max_reconnect_attempts(PROTOCOL_MAX_RECONNECT_ATTEMPTS)
    , _reconnect_exhausted(false)
    , _intentional_disconnect(false)
    , _connect_mutex(xSemaphoreCreateMutex())
    , _connect_in_progress(false)
{
    instance = this;
    mbedtls_aes_init(&_aes_ctx);
    memset(_aes_nonce, 0, sizeof(_aes_nonce));
    _backoff.reset();
}

MQTTClient::~MQTTClient() {
    closeAudioChannel();
    disconnectMqtt();

    if (_mqtt_client) {
        delete _mqtt_client;
    }
    if (_secure_client) {
        delete _secure_client;
    }
    if (_client && !_use_ssl) {
        delete _client;
    }

    mbedtls_aes_free(&_aes_ctx);

    if (instance == this) {
        instance = nullptr;
    }
    if (_connect_mutex) {
        vSemaphoreDelete(_connect_mutex);
        _connect_mutex = nullptr;
    }

}

// ============================================================================
// Lifecycle Methods
// ============================================================================

bool MQTTClient::start() {
    ESP_LOGI(TAG, "Starting MQTT client...");
    ESP_LOGI(TAG, "✓ MQTT client initialized (waiting for configuration)");
    return true;
}

bool MQTTClient::configure(const struct ProtocolConfig& config) {
    if (!config.hasMqtt || config.mqttBroker.length() == 0) {
        ESP_LOGE(TAG, "MQTT configuration not available");
        return false;
    }

    _intentional_disconnect = false;
    ESP_LOGI(TAG, "Configuring MQTT client...");
    ESP_LOGI(TAG, "  Broker: %s:%d", config.mqttBroker.c_str(), config.mqttPort);

    // Determine if SSL should be used (typically port 8883 uses SSL, 1883 does not)
    _use_ssl = (config.mqttPort == 8883);
    _broker = config.mqttBroker;
    _port = config.mqttPort;
    _client_id = config.mqttClientId;
    _username = config.mqttUsername;
    _password = config.mqttPassword;
    _publish_topic = config.mqttTopicPrefix;
    String mac = WiFi.macAddress();
    mac.toLowerCase();
    mac.replace(":", "_");
    if (_publish_topic.length() > 0 && _client_id.length() > 0) {
        _subscribe_topic = _publish_topic + "/p2p/" + _client_id;
    } else {
        _subscribe_topic = "devices/p2p/" + mac;
    }

    ESP_LOGI(TAG, "  Publish topic: %s", _publish_topic.c_str());
    ESP_LOGI(TAG, "  Subscribe topic: %s", _subscribe_topic.c_str());

    setupMqttClient();
    _backoff.reset();
    _reconnect_exhausted = false;

    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    if (_connect_mutex && xSemaphoreTake(_connect_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "MQTT connect already in progress");
        return false;
    }
    _connect_in_progress = true;
    bool connected = connectMqtt();
    _connect_in_progress = false;
    if (_connect_mutex) {
        xSemaphoreGive(_connect_mutex);
    }
    if (!connected) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        if (_on_network_error) {
            _on_network_error("Failed to connect to MQTT broker");
        }
        _backoff.increment();
        return false;
    }

    _mqtt_connected = true;
    _backoff.reset();
    _reconnect_exhausted = false;
    ESP_LOGI(TAG, "✓ MQTT client configured and connected");
    return true;
}

bool MQTTClient::openAudioChannel() {
    if (!_mqtt_connected || !_mqtt_client || !_mqtt_client->connected()) {
        ESP_LOGE(TAG, "Cannot open audio channel: MQTT not connected");
        if (_on_network_error) {
            _on_network_error("MQTT not connected");
        }
        return false;
    }

    ESP_LOGI(TAG, "Opening audio channel...");

    _hello_received = false;
    _session_id = "";
    _local_sequence = 0;
    _remote_sequence = 0;

    if (!publishHello()) {
        ESP_LOGE(TAG, "Failed to send hello message");
        if (_on_network_error) {
            _on_network_error("Failed to send hello message");
        }
        return false;
    }

    ESP_LOGI(TAG, "Hello message sent, waiting for server response...");
    return true;
}

void MQTTClient::closeAudioChannel() {
    ESP_LOGI(TAG, "Closing audio channel (hello_received=%d, udp_connected=%d, session_id=%s)",
             _hello_received, _udp_connected, _session_id.c_str());

    // Disconnect UDP first
    disconnectUdp();

    // Send goodbye message if we have an active session
    if (_mqtt_connected && _mqtt_client && _mqtt_client->connected() && !_session_id.isEmpty()) {
        String message = "{\"session_id\":\"" + _session_id + "\",\"type\":\"goodbye\"}";
        sendMessage(message);
    }

    // Reset state flags
    _hello_received = false;
    _aes_initialized = false;
    _session_id = "";  // Clear session ID for clean slate

    // Notify callback
    if (_on_audio_channel_closed) {
        _on_audio_channel_closed();
    }

    ESP_LOGI(TAG, "Audio channel closed");
}

bool MQTTClient::isAudioChannelOpened() const {
    return _mqtt_connected && _udp_connected && _hello_received && !_error_occurred;
}

// ============================================================================
// MQTT Methods (ArduinoMqttClient usage)
// ============================================================================

void MQTTClient::setupMqttClient() {
    if (_mqtt_client) {
        delete _mqtt_client;
        _mqtt_client = nullptr;
    }
    if (_secure_client) {
        delete _secure_client;
        _secure_client = nullptr;
    }
    if (_client) {
        delete _client;
        _client = nullptr;
    }

    if (_use_ssl) {
        _secure_client = new WiFiClientSecure();
        _secure_client->setInsecure();
        _secure_client->setTimeout(CONNECT_TIMEOUT_MS);
        _mqtt_client = new MqttClient(*_secure_client);
    } else {
        _client = new WiFiClient();
        _client->setTimeout(CONNECT_TIMEOUT_MS);
        _mqtt_client = new MqttClient(*_client);
    }

    _mqtt_client->setKeepAliveInterval(240000);
    _mqtt_client->setConnectionTimeout(CONNECT_TIMEOUT_MS);
    _mqtt_client->setTxPayloadSize(2048);
    _mqtt_client->onMessage(staticMqttCallback);
}

bool MQTTClient::connectMqtt() {
    if (!_mqtt_client) {
        ESP_LOGE(TAG, "MQTT client not initialized. Call setupMqttClient() first.");
        return false;
    }

    if (_mqtt_client->connected()) {
        ESP_LOGI(TAG, "Already connected to MQTT broker");
        return true;
    }

    ESP_LOGI(TAG, "Connecting to MQTT broker: %s:%d (SSL: %s)",
             _broker.c_str(), _port, _use_ssl ? "yes" : "no");
    ESP_LOGI(TAG, "Client ID: %s (length: %d)", _client_id.c_str(), _client_id.length());
    ESP_LOGI(TAG, "Username: %s", _username.length() > 0 ? "***" : "(none)");

    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGE(TAG, "WiFi not connected. Cannot connect to MQTT broker.");
        return false;
    }
    ESP_LOGI(TAG, "WiFi connected: %s, IP: %s, RSSI: %ld dBm",
             WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(),
             WiFi.RSSI());

    if (_mqtt_client->connected()) {
        _mqtt_client->stop();
        delay(500);
    }

    if (_client) {
        _client->stop();
    }
    if (_secure_client) {
        _secure_client->stop();
    }
    delay(200);

    const int max_retries = MAX_CONNECT_RETRIES;
    bool connected = false;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        if (attempt > 1) {
            ESP_LOGI(TAG, "Retry attempt %d/%d...", attempt, max_retries);
            if (_client) {
                _client->stop();
            }
            if (_secure_client) {
                _secure_client->stop();
            }
            for (int i = 0; i < attempt * 100; ++i) {
                esp_task_wdt_reset();
                delay(10);
            }
        }

        esp_task_wdt_reset();
        delay(100);

        unsigned long attempt_start_time = millis();
        ESP_LOGI(TAG, "Attempt %d: Calling connect()...", attempt);

        esp_task_wdt_reset();
        if (_client_id.length() > 0) {
            _mqtt_client->setId(_client_id);
        }
        if (_username.length() > 0) {
            _mqtt_client->setUsernamePassword(_username, _password);
        } else {
            _mqtt_client->setUsernamePassword("", "");
        }

        connected = _mqtt_client->connect(_broker.c_str(), _port);

        unsigned long attempt_duration = millis() - attempt_start_time;

        if (connected) {
            ESP_LOGI(TAG, "✓ Connected in %lu ms", attempt_duration);
            break;
        }

        esp_task_wdt_reset();
        int error = _mqtt_client->connectError();
        ESP_LOGW(TAG, "Connection attempt %d failed after %lu ms. Error: %d, RSSI: %ld dBm",
                 attempt, attempt_duration, error, WiFi.RSSI());

        if (WiFi.status() != WL_CONNECTED) {
            ESP_LOGE(TAG, "WiFi disconnected during connection attempt");
            return false;
        }
    }

    if (connected) {
        ESP_LOGI(TAG, "✓ Connected to MQTT broker");

        if (_subscribe_topic.length() > 0) {
            bool subscribed = _mqtt_client->subscribe(_subscribe_topic.c_str());
            if (subscribed) {
                ESP_LOGI(TAG, "✓ Subscribed to: %s", _subscribe_topic.c_str());
            } else {
                ESP_LOGE(TAG, "✗ Subscribe failed: %s", _subscribe_topic.c_str());
                if (_on_network_error) {
                    _on_network_error("MQTT subscribe failed");
                }
            }
        } else {
            ESP_LOGW(TAG, "Subscribe topic not set; inbound MQTT may be lost");
        }

        _hello_received = false;
        _session_id = "";
        _intentional_disconnect = false;

        if (_on_connected) {
            _on_connected();
        }

        return true;
    }

    int error = _mqtt_client->connectError();
    ESP_LOGE(TAG, "✗ Failed to connect after %d attempts. Final error: %d", max_retries, error);
    if (_on_network_error) {
        _on_network_error("Failed to connect to MQTT broker");
    }

    return false;
}

void MQTTClient::disconnectMqtt() {
    ESP_LOGI(TAG, "Disconnecting MQTT (mqtt_connected=%d, intentional=%d)",
             _mqtt_connected, _intentional_disconnect);

    if (_mqtt_client && _mqtt_client->connected()) {
        _mqtt_client->stop();
        ESP_LOGI(TAG, "Disconnected from MQTT broker");

        if (_on_disconnected) {
            _on_disconnected();
        }
    }
    _mqtt_connected = false;
}

bool MQTTClient::isConnected() const {
    return _mqtt_client && _mqtt_client->connected();
}

bool MQTTClient::sendMessage(const String& message) {
    if (!isConnected()) {
        ESP_LOGE(TAG, "Cannot send message: not connected");
        if (_on_network_error) {
            _on_network_error("MQTT not connected");
        }
        return false;
    }

    if (_publish_topic.length() == 0) {
        ESP_LOGE(TAG, "Cannot send message: publish topic not set");
        if (_on_network_error) {
            _on_network_error("MQTT publish topic not set");
        }
        return false;
    }

    if (!_mqtt_client->beginMessage(_publish_topic, message.length(), false, 0, false)) {
        ESP_LOGE(TAG, "Failed to begin MQTT message");
        if (_on_network_error) {
            _on_network_error("Failed to begin MQTT message");
        }
        return false;
    }

    size_t written = _mqtt_client->write(reinterpret_cast<const uint8_t*>(message.c_str()),
                                         message.length());
    bool ended = _mqtt_client->endMessage() == 1;

    if (written != message.length() || !ended) {
        ESP_LOGE(TAG, "Failed to publish to %s", _publish_topic.c_str());
        if (_on_network_error) {
            _on_network_error("Failed to publish MQTT message");
        }
        return false;
    }

    ESP_LOGD(TAG, "Published to %s: %s", _publish_topic.c_str(), message.c_str());
    return true;
}

void MQTTClient::staticMqttCallback(int message_size) {
    if (instance) {
        instance->handleMqttMessage(message_size);
    }
}

void MQTTClient::handleMqttMessage(int message_size) {
    String topic = _mqtt_client ? _mqtt_client->messageTopic() : "";
    ESP_LOGI(TAG, "[MQTT RX] topic=%s payload_len=%d", topic.c_str(), message_size);

    String payload_str;
    if (message_size > 0) {
        payload_str.reserve(message_size + 1);
        int bytes_read = 0;
        while (_mqtt_client && bytes_read < message_size) {
            int ch = _mqtt_client->read();
            if (ch < 0) {
                break;
            }
            payload_str += static_cast<char>(ch);
            bytes_read++;
        }
    }

    ESP_LOGD(TAG, "MQTT message: %s", payload_str.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload_str);

    if (doc.overflowed()) {
        ESP_LOGE(TAG, "JSON document overflowed - not enough memory");
        if (_on_incoming_json) {
            cJSON* root = cJSON_Parse(payload_str.c_str());
            if (root) {
                _on_incoming_json(root);
                cJSON_Delete(root);
            }
        }
        return;
    }

    if (error) {
        ESP_LOGE(TAG, "Failed to parse MQTT message: %s", error.c_str());
        if (_on_incoming_json) {
            cJSON* root = cJSON_Parse(payload_str.c_str());
            if (root) {
                _on_incoming_json(root);
                cJSON_Delete(root);
            }
        }
        return;
    }

    if (doc.isNull()) {
        ESP_LOGE(TAG, "Parsed document is null");
        if (_on_incoming_json) {
            cJSON* root = cJSON_Parse(payload_str.c_str());
            if (root) {
                _on_incoming_json(root);
                cJSON_Delete(root);
            }
        }
        return;
    }

    if (!doc["type"].is<String>()) {
        ESP_LOGE(TAG, "Message type missing");
        if (_on_incoming_json) {
            cJSON* root = cJSON_Parse(payload_str.c_str());
            if (root) {
                _on_incoming_json(root);
                cJSON_Delete(root);
            }
        }
        return;
    }

    String type = doc["type"].as<String>();

    if (type == "hello") {
        ESP_LOGI(TAG, "Server hello payload: %s", payload_str.c_str());
        parseServerHello(doc);
        if (_on_incoming_json) {
            cJSON* root = cJSON_Parse(payload_str.c_str());
            if (root) {
                _on_incoming_json(root);
                cJSON_Delete(root);
            }
        }
        return;
    } else if (type == "goodbye") {
        String msg_session_id = doc["session_id"].is<String>() ? doc["session_id"].as<String>() : "";
        if (msg_session_id == _session_id || msg_session_id.length() == 0) {
            ESP_LOGI(TAG, "Goodbye received (payload=%s)", payload_str.c_str());
        }
    }

    if (_on_incoming_json) {
        cJSON* root = cJSON_Parse(payload_str.c_str());
        if (root) {
            _on_incoming_json(root);
            cJSON_Delete(root);
        }
    }
}

// ============================================================================
// Hello Message
// ============================================================================

String MQTTClient::buildHelloMessage() {
    JsonDocument doc;
    doc["type"] = "hello";
    doc["version"] = 3;
    doc["transport"] = "udp";

    JsonObject features = doc["features"].to<JsonObject>();
    features["mcp"] = true;
    features["aec"] = true;

    JsonObject audio_params = doc["audio_params"].to<JsonObject>();
    audio_params["format"] = "opus";
    audio_params["sample_rate"] = AUDIO_SAMPLE_RATE_STT;
    audio_params["channels"] = 1;
    audio_params["frame_duration"] = AUDIO_OPUS_FRAME_MS;

    String message;
    serializeJson(doc, message);
    return message;
}

bool MQTTClient::publishHello() {
    String message = buildHelloMessage();

    ESP_LOGI(TAG, "\n:::: Publishing MQTT Hello Message ::::");
    ESP_LOGI(TAG, "[TX] Topic: %s", _publish_topic.c_str());
    ESP_LOGI(TAG, "[TX] Payload length: %d bytes", message.length());
    ESP_LOGI(TAG, "[TX] Payload: %s", message.c_str());

    if (sendMessage(message)) {
        ESP_LOGI(TAG, "[TX] ✓ Message published successfully");
        return true;
    }

    ESP_LOGE(TAG, "[TX] ✗ Failed to publish message");
    return false;
}

void MQTTClient::parseServerHelloCommon(const JsonDocument& doc) {
    if (doc["session_id"].is<String>()) {
        _session_id = doc["session_id"].as<String>();
    }

    if (doc["audio_params"].is<JsonObject>()) {
        if (doc["audio_params"]["sample_rate"].is<int>()) {
            _server_sample_rate = doc["audio_params"]["sample_rate"].as<int>();
        }
        if (doc["audio_params"]["frame_duration"].is<int>()) {
            _server_frame_duration = doc["audio_params"]["frame_duration"].as<int>();
        }
    }

    _hello_received = true;
}

void MQTTClient::parseServerHello(const JsonDocument& doc) {
    if (!doc["transport"].is<String>() || doc["transport"].as<String>() != "udp") {
        ESP_LOGE(TAG, "Unsupported transport");
        return;
    }

    parseServerHelloCommon(doc);

    if (!doc["udp"]["server"].is<String>()) {
        ESP_LOGE(TAG, "No UDP configuration in server hello");
        return;
    }

    String udp_server = doc["udp"]["server"].as<String>();
    int udp_port = doc["udp"]["port"].as<int>();
    String udp_key = doc["udp"]["key"].as<String>();
    String udp_nonce = doc["udp"]["nonce"].as<String>();

    if (udp_server.length() == 0 || udp_key.length() == 0 || udp_nonce.length() == 0) {
        ESP_LOGE(TAG, "UDP configuration incomplete");
        return;
    }

    ESP_LOGI(TAG, "Server hello: %s:%d", udp_server.c_str(), udp_port);

    initializeAES(udp_key, udp_nonce);

    if (!connectUdp(udp_server, udp_port)) {
        ESP_LOGE(TAG, "Failed to connect UDP");
        return;
    }

    _udp_connected = true;

    ESP_LOGI(TAG, "Audio channel opened: %s:%d", udp_server.c_str(), udp_port);

    if (_on_audio_channel_opened) {
        _on_audio_channel_opened();
    }
}

// ============================================================================
// UDP Methods (direct WiFiUDP usage)
// ============================================================================

bool MQTTClient::connectUdp(const String& server, int port) {
    ESP_LOGI(TAG, "Connecting to UDP server: %s:%d", server.c_str(), port);

    _udp_server = server;
    _udp_port = port;

    if (!_udp.begin(0)) {
        ESP_LOGE(TAG, "Failed to start UDP");
        _udp_connected = false;
        return false;
    }

    _udp_connected = true;
    ESP_LOGI(TAG, "✓ UDP client started");
    return true;
}

void MQTTClient::disconnectUdp() {
    if (_udp_connected) {
        _udp.stop();
        _udp_connected = false;
        ESP_LOGI(TAG, "UDP client stopped");
    }
}

bool MQTTClient::isUdpConnected() const {
    return _udp_connected;
}

int MQTTClient::sendUdp(const uint8_t* data, size_t len) {
    if (!_udp_connected) {
        ESP_LOGE(TAG, "Cannot send: UDP not connected");
        return -1;
    }

    if (!_udp.beginPacket(_udp_server.c_str(), _udp_port)) {
        ESP_LOGE(TAG, "Failed to begin UDP packet");
        return -1;
    }

    size_t written = _udp.write(data, len);
    if (written != len) {
        ESP_LOGW(TAG, "Only wrote %d of %d bytes", written, len);
    }

    if (!_udp.endPacket()) {
        ESP_LOGE(TAG, "Failed to send UDP packet");
        return -1;
    }

    ESP_LOGD(TAG, "Sent %d bytes to %s:%d", written, _udp_server.c_str(), _udp_port);
    return written;
}

void MQTTClient::pollUdp() {
    if (!_udp_connected) {
        return;
    }

    int packet_size = _udp.parsePacket();
    if (packet_size > 0) {
        if (packet_size > (int)UDP_MAX_PACKET_SIZE) {
            ESP_LOGW(TAG, "Packet too large (%d bytes), truncating to %d",
                     packet_size, UDP_MAX_PACKET_SIZE);
            packet_size = UDP_MAX_PACKET_SIZE;
        }

        int len = _udp.read(_udp_buffer, packet_size);
        if (len > 0) {
            ESP_LOGD(TAG, "Received %d bytes from %s:%d",
                     len, _udp.remoteIP().toString().c_str(), _udp.remotePort());

            handleUdpPacket(_udp_buffer, len);
        }
    }
}

void MQTTClient::handleUdpPacket(const uint8_t* data, size_t len) {
    if (len < 17) {
        ESP_LOGW(TAG, "UDP packet too small: %d bytes", len);
        return;
    }

    if (data[0] != 0x01) {
        ESP_LOGW(TAG, "Invalid packet type: 0x%02X (expected 0x01)", data[0]);
        return;
    }

    uint8_t flags = data[1];
    if (flags != 0x00) {
        ESP_LOGD(TAG, "UDP packet flags set: 0x%02X", flags);
    }

    uint16_t payload_len = ntohs(*(uint16_t*)&data[2]);
    uint32_t timestamp = ntohl(*(uint32_t*)&data[8]);
    uint32_t sequence = ntohl(*(uint32_t*)&data[12]);
    size_t encrypted_len = len - 16;
    if (payload_len != encrypted_len) {
        ESP_LOGW(TAG, "UDP payload length mismatch: header=%u, actual=%u",
                 payload_len, (unsigned int)encrypted_len);
        if (payload_len > encrypted_len) {
            return;
        }
        encrypted_len = payload_len;
    }

    if (_remote_sequence == 0) {
        ESP_LOGI(TAG, "First UDP packet received, initializing sequence to %lu", sequence);
    } else if (sequence < _remote_sequence) {
        ESP_LOGW(TAG, "Old packet (replay?): seq=%lu, last=%lu", sequence, _remote_sequence);
        return;
    } else if (sequence != _remote_sequence + 1) {
        ESP_LOGW(TAG, "Out of order: seq=%lu, expected=%lu (gap: %lu)",
                 sequence, _remote_sequence + 1, sequence - _remote_sequence - 1);
    }

    uint8_t* decrypted = (uint8_t*)heap_caps_malloc(encrypted_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!decrypted) {
        ESP_LOGE(TAG, "Failed to allocate decrypt buffer");
        return;
    }

    const uint8_t* nonce = data;
    const uint8_t* encrypted = data + 16;

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    int ret = mbedtls_aes_crypt_ctr(&_aes_ctx, encrypted_len, &nc_off,
                                   (uint8_t*)nonce, stream_block,
                                   encrypted, decrypted);

    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to decrypt audio, mbedtls error: %d", ret);
        heap_caps_free(decrypted);
        return;
    }

    AudioStreamPacket* packet = new AudioStreamPacket();
    packet->sample_rate = _server_sample_rate;
    packet->frame_duration = _server_frame_duration;
    packet->timestamp = timestamp;
    packet->payload.assign(decrypted, decrypted + encrypted_len);
    heap_caps_free(decrypted);

    _remote_sequence = sequence;

    ESP_LOGD(TAG, "[AUDIO RX] Received UDP packet: %d bytes encrypted, seq: %lu, ts: %lu",
             encrypted_len, sequence, timestamp);

    if (_on_incoming_audio) {
        _on_incoming_audio(packet);
    } else {
        delete packet;
    }
}

// ============================================================================
// Audio Methods
// ============================================================================

bool MQTTClient::sendAudio(AudioStreamPacket* packet) {
    if (!isAudioChannelOpened()) {
        delete packet;
        return false;
    }

    if (!_aes_initialized) {
        ESP_LOGE(TAG, "Cannot send audio: AES not initialized");
        delete packet;
        return false;
    }

    uint32_t seq = ++_local_sequence;
    uint16_t payload_len = packet->payload.size();
    uint32_t timestamp = packet->timestamp;

    uint8_t nonce[16];
    memcpy(nonce, _aes_nonce, 16);
    nonce[0] = 0x01;  // Packet type
    nonce[1] = 0x00;  // Flags (unused)
    *(uint16_t*)&nonce[2] = htons(payload_len);
    *(uint32_t*)&nonce[8] = htonl(timestamp);
    *(uint32_t*)&nonce[12] = htonl(seq);

    size_t total_size = 16 + payload_len;
    uint8_t* udp_packet = (uint8_t*)heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!udp_packet) {
        ESP_LOGE(TAG, "Failed to allocate UDP packet buffer");
        delete packet;
        return false;
    }

    memcpy(udp_packet, nonce, 16);

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    int ret = mbedtls_aes_crypt_ctr(&_aes_ctx, payload_len, &nc_off,
                                   nonce, stream_block,
                                   packet->payload.data(),
                                   udp_packet + 16);

    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to encrypt audio, mbedtls error: %d", ret);
        heap_caps_free(udp_packet);
        delete packet;
        return false;
    }

    int sent = sendUdp(udp_packet, total_size);
    heap_caps_free(udp_packet);

    delete packet;

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send UDP packet");
        return false;
    }

    // Log every 500 packets or every 5 seconds
    static uint32_t last_tx_log = 0;
    if (seq % 500 == 0 || millis() - last_tx_log > 5000) {
        ESP_LOGI(TAG, "TX #%lu", seq);
        last_tx_log = millis();
    }
    return true;
}

// ============================================================================
// AES Encryption
// ============================================================================

void MQTTClient::initializeAES(const String& hex_key, const String& hex_nonce) {
    ESP_LOGI(TAG, "Initializing AES encryption...");

    String key = decodeHexString(hex_key);
    String nonce = decodeHexString(hex_nonce);

    if (key.length() != 16) {
        ESP_LOGE(TAG, "Invalid key length: %d (expected 16)", key.length());
        return;
    }

    if (nonce.length() != 16) {
        ESP_LOGE(TAG, "Invalid nonce length: %d (expected 16)", nonce.length());
        return;
    }

    mbedtls_aes_setkey_enc(&_aes_ctx, (const unsigned char*)key.c_str(), 128);

    memcpy(_aes_nonce, nonce.c_str(), 16);

    _aes_initialized = true;
    ESP_LOGI(TAG, "✓ AES encryption initialized");
}

String MQTTClient::decodeHexString(const String& hex_string) {
    String decoded;
    decoded.reserve(hex_string.length() / 2);

    for (size_t i = 0; i < hex_string.length(); i += 2) {
        char high = hex_string[i];
        char low = hex_string[i + 1];

        uint8_t byte = 0;
        if (high >= '0' && high <= '9') byte |= (high - '0') << 4;
        else if (high >= 'A' && high <= 'F') byte |= (high - 'A' + 10) << 4;
        else if (high >= 'a' && high <= 'f') byte |= (high - 'a' + 10) << 4;

        if (low >= '0' && low <= '9') byte |= (low - '0');
        else if (low >= 'A' && low <= 'F') byte |= (low - 'A' + 10);
        else if (low >= 'a' && low <= 'f') byte |= (low - 'a' + 10);

        decoded += (char)byte;
    }

    return decoded;
}

// ============================================================================
// Polling
// ============================================================================

void MQTTClient::poll() {
    if (_mqtt_client) {
        if (!_mqtt_client->connected()) {
            if (_intentional_disconnect) {
                return;
            }
            if (_auto_reconnect) {
                if (_max_reconnect_attempts > 0 &&
                    _backoff.attempt_count >= _max_reconnect_attempts) {
                    if (!_reconnect_exhausted) {
                        _reconnect_exhausted = true;
                        _auto_reconnect = false;
                        ESP_LOGE(TAG, "MQTT reconnect attempts exhausted (%u)",
                                 _backoff.attempt_count);
                        if (_on_network_error) {
                            _on_network_error("reconnect_exhausted");
                        }
                        disconnect();
                    }
                    return;
                }
                unsigned long now = millis();
                if (_backoff.attempt_count > 0 && !_backoff.shouldRetry(now)) {
                    return;
                }
                _backoff.last_attempt_time = now;
                ESP_LOGI(TAG, "Attempting MQTT reconnection (attempt %u)...",
                         _backoff.attempt_count + 1);
                bool lock_taken = false;
                if (_connect_mutex) {
                    lock_taken = xSemaphoreTake(_connect_mutex, 0) == pdTRUE;
                }
                if (lock_taken) {
                    _connect_in_progress = true;
                    bool connected = connectMqtt();
                    _connect_in_progress = false;
                    xSemaphoreGive(_connect_mutex);
                    if (connected) {
                        _backoff.reset();
                        if (_on_reconnected) {
                            _on_reconnected();
                        }
                    } else {
                        _backoff.increment();
                    }
                }
            }
            return;
        }
        _mqtt_client->poll();
        if (_backoff.attempt_count > 0) {
            _backoff.reset();
        }
    }

    pollUdp();
}

// ============================================================================
// Callbacks
// ============================================================================

void MQTTClient::onIncomingJson(std::function<void(const cJSON* root)> callback) {
    _on_incoming_json = callback;
}

void MQTTClient::onIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback) {
    _on_incoming_audio = callback;
}

void MQTTClient::onAudioChannelOpened(std::function<void()> callback) {
    _on_audio_channel_opened = callback;
}

void MQTTClient::onAudioChannelClosed(std::function<void()> callback) {
    _on_audio_channel_closed = callback;
}

void MQTTClient::onNetworkError(std::function<void(const String& message)> callback) {
    _on_network_error = callback;
}

void MQTTClient::onConnected(std::function<void()> callback) {
    _on_connected = callback;
}

void MQTTClient::onDisconnected(std::function<void()> callback) {
    _on_disconnected = callback;
}

void MQTTClient::onReconnected(std::function<void()> callback) {
    _on_reconnected = callback;
}

void MQTTClient::disconnect() {
    _intentional_disconnect = true;
    closeAudioChannel();
    disconnectMqtt();
}
