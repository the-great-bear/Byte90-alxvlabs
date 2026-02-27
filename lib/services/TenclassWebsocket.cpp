/**
 * TenclassWebsocket.cpp
 *
 * Implementation for TenclassWebsocket.
 */

#include "TenclassWebsocket.h"
#include "DeviceConfig.h"
#include "NvsStorage.h"
#include "WebsocketClient.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <arpa/inet.h>

static const char* TAG = "TenclassWebsocket";

TenclassWebsocket::TenclassWebsocket(NVSStorage* storage)
    : wsClient_(nullptr)
    , _storage(storage)
    , server_sample_rate_(24000)
    , server_frame_duration_(60)
    , hello_received_(false)
    , protocol_version_(1)
    , hello_sent_(false)
    , audio_channel_opened_(false)
    , intentional_disconnect_(true)  // Start disconnected (will connect on-demand)
{
    memset(&config_, 0, sizeof(config_));
}

TenclassWebsocket::~TenclassWebsocket() {
    CloseAudioChannel();
    if (wsClient_) {
        delete wsClient_;
        wsClient_ = nullptr;
    }
}

bool TenclassWebsocket::Start() {
    // WebSocket doesn't need a separate Start() - it's done in Configure()
    return true;
}

bool TenclassWebsocket::Configure(const struct ProtocolConfig& config) {
    config_ = config;
    protocol_version_ = config.wsVersion;
    
    
    setupWebSocket();
    return true;
}

void TenclassWebsocket::setupWebSocket() {
    if (wsClient_) {
        delete wsClient_;
    }


    String clientId = _storage->getDeviceUUID();
    ESP_LOGD(TAG, "Using UUID as Client-Id: %s", clientId.c_str());
    
    String macAddr = WiFi.macAddress();
    macAddr.toLowerCase();
    String authToken = "Bearer " + config_.wsToken;
    String headers = "Authorization: " + authToken + "\r\n" +
                     "Protocol-Version: " + String(config_.wsVersion) + "\r\n" +
                     "Device-Id: " + macAddr + "\r\n" +
                     "Client-Id: " + clientId;


    wsClient_ = new WebSocketClient();
    wsClient_->setMaxReconnectAttempts(PROTOCOL_MAX_RECONNECT_ATTEMPTS);

    ESP_LOGD(TAG, ":::: SETUP: WebSocket Configuration ::::");
    ESP_LOGD(TAG, "Token: %s (Length: %d)", config_.wsToken.c_str(), config_.wsToken.length());
    ESP_LOGD(TAG, "Host: %s, Port: %d, Path: %s", config_.wsHost.c_str(), config_.wsPort, config_.wsPath.c_str());
    ESP_LOGD(TAG, "Protocol Version: %d", config_.wsVersion);
    ESP_LOGD(TAG, ":::: SETUP: Headers: %s ::::", headers.c_str());
    
    wsClient_->setExtraHeaders(headers.c_str());
    
    // Set up event callback
    wsClient_->onEvent([this](WSTYPE_t type, uint8_t* payload, size_t length) {
        handleWebSocketEvent(type, payload, length);
    });
    
    // Reduce buffer sizes to save internal RAM
    // Default is 16KB, reduce to 4KB for both RX and TX
    // Heartbeat disabled - not needed for short on-demand connections (server has 60s timeout)
    
    ESP_LOGD(TAG, "Calling beginSSL() at %lu ms", millis());

    // Use certificate validation for secure connection
    wsClient_->beginSSL(config_.wsHost.c_str(), config_.wsPort,
                        config_.wsPath.c_str(), ROOT_CA_CERTIFICATE, "");

    
    session_id_ = "";
    hello_sent_ = false;
    hello_received_ = false;  // hello_received_ is in base class
}

bool TenclassWebsocket::OpenAudioChannel() {
    if (!wsClient_) {
        ESP_LOGE(TAG, "WebSocket not configured");
        return false;
    }

    // Reset state (hello_received_ and session_id_ are in base class)
    hello_sent_ = false;
    hello_received_ = false;
    session_id_ = "";
    intentional_disconnect_ = false;  // We're intentionally connecting, allow reconnect

    // Send hello message (will be sent automatically on connect, but also try here if already connected)
    if (wsClient_->isConnected() && !hello_sent_) {
        if (!sendHello()) {
            ESP_LOGE(TAG, "❌ Failed to send hello message");
            return false;
        }
    }

    return true;
}

void TenclassWebsocket::CloseAudioChannel() {
    ESP_LOGI(TAG, "Closing audio channel (audio_channel_opened=%d, hello_received=%d)",
             audio_channel_opened_, hello_received_);

    // Mark as intentional disconnect to prevent auto-reconnect
    intentional_disconnect_ = true;

    // Disconnect WebSocket
    if (wsClient_) {
        wsClient_->disconnect();
    }

    // Reset state flags
    audio_channel_opened_ = false;
    hello_sent_ = false;
    hello_received_ = false;
    session_id_ = "";  // Clear session ID for clean slate

    ESP_LOGI(TAG, "Audio channel closed");
}

bool TenclassWebsocket::IsAudioChannelOpened() const {
    return audio_channel_opened_ && hello_received_ && wsClient_ && wsClient_->isConnected();
}

bool TenclassWebsocket::sendHello() {
    if (!wsClient_ || !wsClient_->isConnected()) {
        ESP_LOGE(TAG, "[TX] ✗ WebSocket not connected");
        return false;
    }

    // Build hello message
    JsonDocument doc;
    doc["type"] = "hello";
    doc["version"] = protocol_version_;
    doc["transport"] = "websocket";

    JsonObject features = doc["features"].to<JsonObject>();
    features["mcp"] = true;
    features["aec"] = true;

    JsonObject audioParams = doc["audio_params"].to<JsonObject>();
    audioParams["format"] = "opus";
    audioParams["sample_rate"] = AUDIO_SAMPLE_RATE_STT;
    audioParams["channels"] = 1;
    audioParams["frame_duration"] = AUDIO_OPUS_FRAME_MS;

    String message;
    serializeJson(doc, message);

    // Send hello message
    ESP_LOGI(TAG, "[WS TX] %s", message.c_str());
    bool sent = wsClient_->sendTXT(message);
    if (sent) {
        hello_sent_ = true;
        ESP_LOGI(TAG, "[TX] ✓ Hello message sent successfully");
        return true;
    } else {
        ESP_LOGE(TAG, "[TX] ✗ Failed to send message");
        return false;
    }
}

void TenclassWebsocket::parseServerHello(const JsonDocument& doc) {
    // Extract session ID
    if (doc["session_id"].is<String>()) {
        session_id_ = doc["session_id"].as<String>();
    }

    // Extract audio params
    if (doc["audio_params"].is<JsonObject>()) {
        if (doc["audio_params"]["sample_rate"].is<int>()) {
            server_sample_rate_ = doc["audio_params"]["sample_rate"].as<int>();
        }
        if (doc["audio_params"]["frame_duration"].is<int>()) {
            server_frame_duration_ = doc["audio_params"]["frame_duration"].as<int>();
        }
    }

    hello_received_ = true;

    ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());

    audio_channel_opened_ = true;

    if (on_audio_channel_opened_) {
        on_audio_channel_opened_();
    }
}

void TenclassWebsocket::handleWebSocketEvent(WSTYPE_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WSTYPE_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket Disconnected");
            
            // Clean up state (safe to call multiple times)
            CloseAudioChannel();
            
            if (on_disconnected_) {
                on_disconnected_();
            }
            break;
            
        case WSTYPE_CONNECTED:

            // if (wsClient_) {
            //     wsClient_->setReconnectInterval(0);  // Disable auto-reconnect (on-demand connection pattern)
            //     }
            
            if (on_connected_) {
                on_connected_();
            }

            // For SSL connections, give extra time for session to stabilize
            // before sending application data
            delay(200);

            // Send hello automatically on connect
            if (!hello_sent_) {
                sendHello();
            }
            break;
            
        case WSTYPE_TEXT:
            {
                ESP_LOGI(TAG, "[WS RX] %.*s", (int)length, (char*)payload);
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, (const char*)payload, length);
                
                if (!error) {
                    const char* type = doc["type"];
                    
                    if (type && strcmp(type, "hello") == 0) {
                        parseServerHello(doc);
                    }
                    
                    // Forward to callback for application-level handling
                    if (on_incoming_json_) {
                        // Convert JsonDocument to cJSON for callback compatibility
                        String jsonStr;
                        serializeJson(doc, jsonStr);
                        cJSON* root = cJSON_Parse(jsonStr.c_str());
                        if (root) {
                            on_incoming_json_(root);
                            cJSON_Delete(root);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "❌ Failed to parse JSON: %s", error.c_str());
                    if (on_network_error_) {
                        on_network_error_(String("JSON parse error: ") + error.c_str());
                    }
                }
            }
            break;
            
        case WSTYPE_BIN:
            handleBinaryPacket(payload, length);
            break;
            
        case WSTYPE_ERROR:
            ESP_LOGE(TAG, "❌ WebSocket Error: %.*s", length, (char*)payload);
            if (on_network_error_) {
                String errorMsg = "WebSocket error: ";
                errorMsg += String((char*)payload, length);
                on_network_error_(errorMsg);
            }
            break;
            
        case WSTYPE_PING:
            ESP_LOGD(TAG, "WebSocket PING received");
            break;
            
        case WSTYPE_PONG:
            ESP_LOGD(TAG, "WebSocket PONG received");
            // This is the response to the PING we sent, confirming the connection is alive.
            // ESP_LOGI(TAG, "💓 WebSocket PONG received from server"); // Keep this if you want to see pongs
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown WebSocket event type: %d", type);
            break;
    }
}

void TenclassWebsocket::handleBinaryPacket(const uint8_t* payload, size_t length) {
    const uint8_t* audioData = payload;
    size_t audioLen = length;
    uint32_t timestamp = 0;
    
    if (protocol_version_ == 2 && length >= sizeof(BinaryProtocol2)) {
        const BinaryProtocol2* protocol = reinterpret_cast<const BinaryProtocol2*>(payload);
        size_t payload_size = ntohl(protocol->payload_size);
        size_t header_size = sizeof(BinaryProtocol2);
        if (payload_size > length - header_size) {
            ESP_LOGW(TAG, "Invalid v2 payload size: %u (len=%u)",
                     static_cast<unsigned int>(payload_size),
                     static_cast<unsigned int>(length));
            return;
        }
        timestamp = ntohl(protocol->timestamp);
        audioData = protocol->payload;
        audioLen = payload_size;
        ESP_LOGD(TAG, "Extracted timestamp: %lu, payload size: %d", timestamp, audioLen);
    }
    
    if (on_incoming_audio_) {
        AudioStreamPacket* packet = new AudioStreamPacket();
        packet->sample_rate = server_sample_rate_;
        packet->frame_duration = server_frame_duration_;
        packet->timestamp = timestamp;
        packet->payload.assign(audioData, audioData + audioLen);
        
        on_incoming_audio_(packet);
    }
}

bool TenclassWebsocket::SendAudio(AudioStreamPacket* packet) {
    if (!packet || !wsClient_ || !wsClient_->isConnected()) {
        if (packet) delete packet;
        return false;
    }

    static uint32_t last_tx_log_ms = 0;
    static uint32_t tx_packets = 0;
    uint32_t now_ms = millis();
    tx_packets++;
    if (now_ms - last_tx_log_ms > 2000) {
        ESP_LOGI(TAG, "[WS AUDIO TX] packets=%lu last_size=%u",
                 tx_packets,
                 static_cast<unsigned int>(packet->payload.size()));
        last_tx_log_ms = now_ms;
    }
    
    if (protocol_version_ == 2) {
        size_t totalSize = sizeof(BinaryProtocol2) + packet->payload.size();
        constexpr size_t MAX_STACK_PACKET_SIZE =
            sizeof(BinaryProtocol2) + AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE;
        uint8_t stack_data[MAX_STACK_PACKET_SIZE];
        uint8_t* data = stack_data;
        if (totalSize > MAX_STACK_PACKET_SIZE) {
            data = static_cast<uint8_t*>(malloc(totalSize));
            if (data == nullptr) {
                delete packet;
                return false;
            }
        }
        
        BinaryProtocol2* protocol = reinterpret_cast<BinaryProtocol2*>(data);
        protocol->version = htons(2);
        protocol->type = 0;
        protocol->reserved = 0;
        protocol->timestamp = htonl(packet->timestamp);
        protocol->payload_size = htonl(packet->payload.size());
        memcpy(data + sizeof(BinaryProtocol2), packet->payload.data(), packet->payload.size());
        
        bool result = wsClient_->sendBIN(data, totalSize);
        if (data != stack_data) {
            free(data);
        }
        delete packet;  // Always delete packet
        return result;
    } else {
        bool result = wsClient_->sendBIN(packet->payload.data(), packet->payload.size());
        delete packet;  // Always delete packet
        return result;
    }
}

bool TenclassWebsocket::sendMessage(const String& message) {
    if (!wsClient_ || !wsClient_->isConnected()) {
        if (on_network_error_) {
            on_network_error_("WebSocket not connected");
        }
        return false;
    }
    ESP_LOGI(TAG, "[WS TX] %s", message.c_str());
    bool sent = wsClient_->sendTXT(message.c_str());
    if (!sent && on_network_error_) {
        on_network_error_("WebSocket send failed");
    }
    return sent;
}

bool TenclassWebsocket::sendBIN(const uint8_t* data, size_t len) {
    if (!wsClient_ || !wsClient_->isConnected()) {
        if (on_network_error_) {
            on_network_error_("WebSocket not connected");
        }
        return false;
    }
    bool sent = wsClient_->sendBIN(data, len);
    if (!sent && on_network_error_) {
        on_network_error_("WebSocket send BIN failed");
    }
    return sent;
}

bool TenclassWebsocket::isConnected() const {
    return wsClient_ && wsClient_->isConnected();
}

void TenclassWebsocket::poll() {
    if (!wsClient_) return;

    // Skip polling ONLY if we intentionally disconnected AND are fully disconnected.
    // If we are still connected, we must loop to allow the close handshake to complete.
    if (intentional_disconnect_ && !wsClient_->isConnected()) {
        return;
    }

    static bool wasConnected = false;
    static unsigned long disconnectTime = 0;
    bool isConnected = wsClient_->isConnected();

    if (wasConnected && !isConnected) {
        disconnectTime = millis();
        ESP_LOGD(TAG, "WebSocket disconnected, waiting for SSL cleanup...");
    }

    unsigned long timeSinceDisconnect = (disconnectTime > 0) ? (millis() - disconnectTime) : 0;
    if (isConnected || !wasConnected || timeSinceDisconnect > 500) {
        // TODO: Implement timeout in wsClient library to prevent blocking longer than watchdog timeout
        wsClient_->loop();
        yield();  // Prevent watchdog timeout
        if (timeSinceDisconnect > 500 && disconnectTime > 0) {
            disconnectTime = 0;
        }
    }

    wasConnected = isConnected;
}

// ============================================================================
// Callback Registration
// ============================================================================

void TenclassWebsocket::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void TenclassWebsocket::OnIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback) {
    on_incoming_audio_ = callback;
}

void TenclassWebsocket::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void TenclassWebsocket::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void TenclassWebsocket::OnNetworkError(std::function<void(const String& message)> callback) {
    on_network_error_ = callback;
}

void TenclassWebsocket::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void TenclassWebsocket::OnDisconnected(std::function<void()> callback) {
    on_disconnected_ = callback;
}
