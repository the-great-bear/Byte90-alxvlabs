/**
 * ProtocolManager.cpp
 *
 * Implementation for ProtocolManager.
 */

#include "ProtocolManager.h"

#include "ApiClient.h"
#include "ApplicationAudio.h"
#include "DeviceConfig.h"
#include "HapticsManager.h"
#include "McpServer.h"
#include "McpToolManager.h"
#include "ProtocolFactory.h"
#include "ProtocolClient.h"
#include "TaskManager.h"
#include "Mp3Player.h"
#include "AudioService.h"

#if USE_MQTT_PROTOCOL
#include "TenclassMQTT.h"
#else
#include "TenclassWebsocket.h"
#include "WebsocketClient.h"
#endif

#include <ArduinoJson.h>
#include <cJSON.h>
#include <esp_log.h>

static const char* TAG = "ProtocolManager";

ProtocolManager::ProtocolManager(
    NVSStorage* storage,
    ProtocolType*& protocol,
    ProtocolConfig& protocol_config,
    SystemStateManager*& state_manager,
    ApplicationAudio*& audio,
    McpServer*& mcp_server,
    bool& protocol_connected,
    bool& protocol_ready,
    bool& pending_listening_start,
    McpToolManager* mcp_tool_manager
)
    : _storage(storage)
    , _protocol(protocol)
    , _protocol_config(protocol_config)
    , _state_manager(state_manager)
    , _audio(audio)
    , _mcp_server(mcp_server)
    , _protocol_connected(protocol_connected)
    , _protocol_ready(protocol_ready)
    , _pending_listening_start(pending_listening_start)
    , _mcp_tool_manager(mcp_tool_manager)
    , _api_client(nullptr)
    , _protocol_client(nullptr)
    , _audio_sink(nullptr)
    , _pending_disconnect_sound(false)
    , _idle_goodbye_in_progress(false)
    , _idle_disconnect_sound_started(false)
    , _idle_goodbye_sent_ms(0)
    , _mqtt_goodbye_pending(false)
    , _disconnect_in_progress(false)
    , _pending_listen_source("")
    , _emotion_callback(nullptr)
    , _network_error_first_ms(0)
    , _network_error_count(0)
    , _audio_congestion_first_ms(0)
    , _audio_congestion_last_ms(0)
    , _mcp_tools_ready(false) {
}

ProtocolManager::~ProtocolManager() {
    delete _api_client;
    delete _protocol_client;
    delete _audio_sink;
}

void ProtocolManager::initializeProtocol() {
    if (_audio && _audio->getService()) {
        _audio->getService()->setSendQueueFullCallback(
            [](void* user_data) {
                if (!user_data) {
                    return;
                }
                auto* self = static_cast<ProtocolManager*>(user_data);
                self->handleNetworkError("audio_send_queue_full");
            },
            this);
    }


#if USE_MQTT_PROTOCOL
    ESP_LOGI(TAG, "9.1. MQTT Protocol...");
    _protocol = ProtocolFactory::createProtocol(_storage);
    _protocol_client = ProtocolFactory::createProtocolClient(_protocol);
    _audio_sink = ProtocolFactory::createAudioSink(_protocol);
#else
    ESP_LOGI(TAG, "9.1. WebSocket Protocol...");
    _protocol = ProtocolFactory::createProtocol(_storage);
    _protocol_client = ProtocolFactory::createProtocolClient(_protocol);
    _audio_sink = ProtocolFactory::createAudioSink(_protocol);
#endif

    _protocol->OnIncomingJson([this](const cJSON* root) {
        handleIncomingJson(root);
    });

    _protocol->OnIncomingAudio([this](AudioStreamPacket* packet) {
        handleIncomingAudio(packet);
    });

    _protocol->OnAudioChannelOpened([this]() {
        _protocol_ready = true;

        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_CONNECTING);
        }

        if (_pending_listening_start) {
            ESP_LOGI(TAG, "✅ Connection complete, starting listening...");
            _pending_listening_start = false;
            startListeningWithSource(_pending_listen_source);
        }

        if (_audio && _audio->getService()) {
            if (_audio->getService()->startPlayback()) {
                delay(100);
                ESP_LOGI(TAG, "Toggle listening mode: Press button to start/stop listening");
            } else {
                ESP_LOGW(TAG, "🟡 Failed to start audio playback");
            }
        }
    });

    _protocol->OnAudioChannelClosed([this]() {
        ESP_LOGI(TAG, "Audio channel closed");
        _protocol_ready = false;

        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_IDLE);
        }

        if (_audio && _audio->getService()) {
            _audio->getService()->stopCapture();
            _audio->getService()->stopPlayback();
        }
    });

    _protocol->OnConnected([this]() {
        _protocol_connected = true;
        _mcp_tools_ready = false;

        if (_mcp_tool_manager) {
            _mcp_tool_manager->ensureWorker();
        }

        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_CONNECTING);
        }
    });

    _protocol->OnDisconnected([this]() {
        ESP_LOGW(TAG, "Protocol disconnected - starting cleanup");

        bool was_listening = _audio && _audio->isListening();
        _mcp_tools_ready = false;
        performDisconnect(was_listening);

        ESP_LOGI(TAG, "Disconnect cleanup complete");
    });

    _protocol->OnNetworkError([this](const String& message) {
        handleNetworkError(message);
    });

#if USE_MQTT_PROTOCOL
    _protocol->OnReconnected([this]() {
        ESP_LOGI(TAG, "MQTT reconnected, reopening audio channel...");
        _protocol_ready = false;
        _pending_listening_start = true;
        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_CONNECTING);
        }
        if (_protocol && !_protocol->IsAudioChannelOpened()) {
            if (!_protocol->OpenAudioChannel()) {
                ESP_LOGE(TAG, "❌ Failed to reopen audio channel after MQTT reconnect");
                performDisconnect(true);
            }
        }
    });
    ESP_LOGI(TAG, "✅ MQTT protocol initialized");
#else
    ESP_LOGI(TAG, "✅ WebSocket protocol initialized");
#endif

    _api_client = new ApiClient(_protocol_client, _storage);
    ESP_LOGI(TAG, "API client initialized");

    if (_mcp_tool_manager) {
        _mcp_tool_manager->setApiClient(_api_client);
        _mcp_tool_manager->setMcpServer(_mcp_server);
        _mcp_tool_manager->ensureWorker();
        _mcp_tool_manager->setToolsListCallback([this]() {
            _mcp_tools_ready = true;
            if (_pending_listening_start && _protocol_ready) {
                ESP_LOGI(TAG, "✅ MCP tools ready, starting listening...");
                _pending_listening_start = false;
                startListeningWithSource(_pending_listen_source);
            }
        });
    }
}

void ProtocolManager::setMcpServer(McpServer* server) {
    _mcp_server = server;
    if (_mcp_tool_manager) {
        _mcp_tool_manager->setMcpServer(server);
    }
}

void ProtocolManager::setEmotionCallback(std::function<void(const String&)> callback) {
    _emotion_callback = callback;
}

void ProtocolManager::setConfigRefreshCallback(std::function<void()> callback) {
    _config_refresh_callback = callback;
}

void ProtocolManager::connectProtocol() {
    if (!_protocol) {
#if USE_MQTT_PROTOCOL
        ESP_LOGE(TAG, "MQTT protocol not initialized");
#else
        ESP_LOGE(TAG, "WebSocket protocol not initialized");
#endif
        return;
    }

    if (!_protocol->Start()) {
#if USE_MQTT_PROTOCOL
        ESP_LOGE(TAG, "❌ Failed to start MQTT protocol");
#else
        ESP_LOGE(TAG, "❌ Failed to start WebSocket protocol");
#endif
        handleNetworkError("protocol_start_failed");
        performDisconnect(true);
        return;
    }

    if (!_protocol->Configure(_protocol_config)) {
#if USE_MQTT_PROTOCOL
        ESP_LOGE(TAG, "❌ Failed to configure MQTT protocol");
#else
        ESP_LOGE(TAG, "❌ Failed to configure WebSocket protocol");
#endif
        handleNetworkError("protocol_config_failed");
        performDisconnect(true);
        return;
    }

    if (!_protocol->OpenAudioChannel()) {
        ESP_LOGE(TAG, "❌ Failed to open audio channel");
        handleNetworkError("protocol_open_audio_failed");
        performDisconnect(true);
        return;
    }
}

void ProtocolManager::handleIncomingJson(const cJSON* root) {
    if (!root) return;

    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) return;

    const char* typeStr = type->valuestring;

    if (strcmp(typeStr, "hello") == 0) {
        // Hello is handled by protocol callbacks
    }
    else if (strcmp(typeStr, "stt") == 0) {
        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            ESP_LOGI(TAG, "[STT] Full JSON: %s", json_str);
            free(json_str);
        }
    }
    else if (strcmp(typeStr, "goodbye") == 0) {
        ESP_LOGI(TAG, "Server said goodbye (session timeout)");
#if USE_MQTT_PROTOCOL
        _mqtt_goodbye_pending = true;
#endif
    }
    else if (strcmp(typeStr, "llm") == 0) {
        const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
        if (emotion && cJSON_IsString(emotion)) {
            ESP_LOGI(TAG, "📱 LLM Response - Emotion received: '%s'", emotion->valuestring);
            if (_emotion_callback) {
                _emotion_callback(String(emotion->valuestring));
            }
        }
    }
    else if (strcmp(typeStr, "tts") == 0) {
        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            ESP_LOGI(TAG, "[TTS] Full JSON: %s", json_str);
            free(json_str);
        }
        const cJSON* state = cJSON_GetObjectItem(root, "state");
        if (state && cJSON_IsString(state)) {
            const char* stateStr = state->valuestring;
            if (_audio) {
                _audio->notifyTtsActivity();
            }
            if (strcmp(stateStr, "start") == 0) {
                if (_audio) {
                    _audio->notifyTtsStart();
                }

                if (_audio && _audio->getService() && _audio->getService()->isCaptureActive()) {
                    _audio->getService()->stopCapture();
                }

                if (_audio && _audio->getCodec() && !_audio->getCodec()->isOutputEnabled()) {
                    _audio->getCodec()->enableOutput(true);
                }
            }
            else if (strcmp(stateStr, "stop") == 0) {
                ESP_LOGD(TAG, "TTS stop - checking listening state: %d", _audio ? _audio->isListening() : 0);
                if (_audio) {
                    _audio->notifyTtsStop();
                }

                if (_audio && _audio->isListening()) {
                    // Small grace period to avoid cutting off the end of TTS.
                    delay(120);
                    if (_audio->getService() && !_audio->getService()->isCaptureActive()) {
                        _audio->getService()->startCapture();
                    }

                    if (_api_client && _protocol) {
                        _api_client->sendListenStart(_protocol->session_id(), true);
                    }

                    if (_audio) {
                        if (_state_manager) {
                            _state_manager->setState(SYSTEM_STATE_LISTENING);
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "TTS complete, disconnecting (on-demand mode)");
                    _pending_listening_start = false;
                    performDisconnect(false);

                    if (_audio) {
                        if (_state_manager) {
                            _state_manager->setState(SYSTEM_STATE_IDLE);
                        }
                    }
                }
            }
        }
    }
    else if (strcmp(typeStr, "mcp") == 0) {
        const cJSON* payload = cJSON_GetObjectItem(root, "payload");
        if (payload && cJSON_IsObject(payload) && _mcp_tool_manager) {
            bool should_enter_loading = false;
            const cJSON* method = cJSON_GetObjectItem(payload, "method");
            if (method && cJSON_IsString(method)) {
                should_enter_loading = (strcmp(method->valuestring, "tools/call") == 0);
            }
            if (method && cJSON_IsString(method) &&
                strcmp(method->valuestring, "tools/call") == 0) {
                const cJSON* params = cJSON_GetObjectItem(payload, "params");
                const cJSON* name = params ? cJSON_GetObjectItem(params, "name") : nullptr;
                if (name && cJSON_IsString(name)) {
                    ESP_LOGI(TAG, "MCP tools/call received: %s", name->valuestring);
                } else {
                    ESP_LOGI(TAG, "MCP tools/call received (missing name)");
                }
            }
            String session_id = "";
            const cJSON* session_item = cJSON_GetObjectItem(root, "session_id");
            if (session_item && cJSON_IsString(session_item)) {
                session_id = session_item->valuestring;
            }
            if (session_id.isEmpty() && _protocol) {
                session_id = _protocol->session_id();
            }
            if (session_id.isEmpty()) {
                ESP_LOGW(TAG, "MCP request received without session_id; responding with empty session_id");
            } else {
                if (_mcp_server) {
                    _mcp_server->setSessionId(session_id);
                }
            }

            char* payload_str = cJSON_PrintUnformatted(payload);
            if (!payload_str) {
                return;
            }

            ESP_LOGI(TAG, "MCP request received from server: %s", payload_str);
            _mcp_tool_manager->enqueueRequest(String(payload_str), session_id, should_enter_loading);
            free(payload_str);
        }
    }
    // Note: "stt" handled above to avoid duplicate logging.
}

void ProtocolManager::handleIncomingAudio(AudioStreamPacket* packet) {
    if (_audio) {
        _audio->handleIncomingAudio(packet);
    } else if (packet) {
        delete packet;
    }
}

void ProtocolManager::getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const {
    ws_client = nullptr;
    hello_received = false;
    if (_protocol) {
        hello_received = _protocol->IsHelloReceived();
#if !USE_MQTT_PROTOCOL
        ws_client = _protocol->getWsClient();
#endif
    }
}

WebSocketClient* ProtocolManager::getWebsocketClient() const {
#if !USE_MQTT_PROTOCOL
    return _protocol ? _protocol->getWsClient() : nullptr;
#else
    return nullptr;
#endif
}

void ProtocolManager::poll() {
    if (_protocol) {
        _protocol->poll();
    }
}

void ProtocolManager::startListening() {
    if (!_mcp_tools_ready) {
        ESP_LOGI(TAG, "⏳ MCP tools not ready; queuing listening start");
        _pending_listening_start = true;
        _pending_listen_source = "";
        return;
    }

    if (!_audio) return;

    _pending_listen_source = "";
    _audio->startListening(_api_client, _protocol ? _protocol->session_id() : "", _state_manager);
}

void ProtocolManager::startListeningWithSource(const String& source) {
    if (!_mcp_tools_ready) {
        ESP_LOGI(TAG, "⏳ MCP tools not ready; queuing listening start");
        _pending_listening_start = true;
        if (!source.isEmpty()) {
            _pending_listen_source = source;
        }
        return;
    }

    if (!_audio) {
        return;
    }

    const String session_id = _protocol ? _protocol->session_id() : "";
    const String listen_source = source.isEmpty() ? _pending_listen_source : source;
    bool started = _audio->startListening(_api_client, session_id, _state_manager);
    // if (listen_source == "button" && _api_client) {
    //     _api_client->sendListenDetect(session_id, "What's up");
    // }
    _pending_listen_source = "";
}

void ProtocolManager::setPendingListenSource(const String& source) {
    _pending_listen_source = source;
    ESP_LOGI(TAG, "Queued listen source: %s", source.c_str());
}

void ProtocolManager::stopListening() {
    if (!_audio || !_audio->isListening()) {
        return;
    }

    ESP_LOGI(TAG, "User stopped listening, sending abort and disconnecting");
    _idle_goodbye_in_progress = false;
    _idle_disconnect_sound_started = false;
    _idle_goodbye_sent_ms = 0;

    if (_api_client && _protocol) {
        _api_client->sendAbort(_protocol->session_id(), "user_stopped");
    }

#if USE_MQTT_PROTOCOL
    if (_protocol && _protocol->getClient()) {
        _protocol->getClient()->disconnect();
    }
#else
    if (_protocol && _protocol->getWsClient()) {
        _protocol->getWsClient()->disconnect();
    }
#endif
}

void ProtocolManager::abortResponse(const String& reason) {
    if (_api_client && _protocol) {
        _api_client->sendAbort(_protocol->session_id(), reason);
    }
}

void ProtocolManager::performDisconnect(bool playSound) {
    if (_disconnect_in_progress) {
        return;
    }
    _disconnect_in_progress = true;

    if (_protocol) {
        _protocol->CloseAudioChannel();
    }

    if (_audio) {
        _audio->resetListeningState();
        if (_audio->getService()) {
            _audio->getService()->stopCapture();
            _audio->getService()->stopPlayback();
        }
    }

    _pending_listening_start = false;
    _protocol_connected = false;
    _protocol_ready = false;
    _idle_goodbye_in_progress = false;
    _idle_disconnect_sound_started = false;
    _idle_goodbye_sent_ms = 0;

    delay(300);

    if (_state_manager) {
        _state_manager->setState(SYSTEM_STATE_IDLE);
    }

    if (playSound && _audio) {
        _audio->playSoundWithHaptic("/sounds/disconnect.mp3", HapticsManager::HAPTIC_EVENT_DISCONNECT);
    }

    ESP_LOGI(TAG, "Cleanup completed");
    _disconnect_in_progress = false;
}

void ProtocolManager::handleNetworkError(const String& message) {
    ESP_LOGE(TAG, "Protocol error: %s", message.c_str());

    if (message.indexOf("audio_send_queue_full") >= 0) {
        const uint32_t now = millis();
        const uint32_t recovery_gap_ms = 2000;
        const uint32_t persistent_ms = 20000;

        if (_audio_congestion_first_ms == 0 ||
            (now - _audio_congestion_last_ms) > recovery_gap_ms) {
            _audio_congestion_first_ms = now;
        }
        _audio_congestion_last_ms = now;

        if ((now - _audio_congestion_first_ms) >= persistent_ms) {
            ESP_LOGW(TAG, "Audio congestion persisted for %lu ms, disconnecting",
                     static_cast<unsigned long>(now - _audio_congestion_first_ms));
            _audio_congestion_first_ms = 0;
            _audio_congestion_last_ms = 0;
            performDisconnect(true);
        }
        return;
    }

    if (!_protocol_ready &&
        (message.indexOf("handshake_timeout") >= 0 ||
         message.indexOf("reconnect_exhausted") >= 0 ||
         message.indexOf("WebSocket error") >= 0)) {
        if (_config_refresh_callback) {
            _config_refresh_callback();
        }
    }

    if (message.indexOf("reconnect_exhausted") >= 0) {
        _network_error_first_ms = 0;
        _network_error_count = 0;
        performDisconnect(true);
        return;
    }

    const uint32_t now = millis();
    const uint32_t window_ms = 5000;
    const uint8_t threshold = 5;

    if (_network_error_first_ms == 0 || (now - _network_error_first_ms) > window_ms) {
        _network_error_first_ms = now;
        _network_error_count = 0;
    }

    _network_error_count++;
    if (_network_error_count >= threshold) {
        _network_error_first_ms = 0;
        _network_error_count = 0;
        performDisconnect(true);
    }
}

void ProtocolManager::updateAudioTransmission() {
    if (!_audio) return;

    _audio->updateTransmission(_audio_sink);
#if USE_MQTT_PROTOCOL
    if (_mqtt_goodbye_pending && _protocol && _protocol->getClient()) {
        bool mp3_active = false;
        if (_audio && _audio->getMp3Player()) {
            mp3_active = _audio->getMp3Player()->isPlaying();
        }
        bool speaking = _state_manager &&
                        _state_manager->getState() == SYSTEM_STATE_SPEAKING;
        if (!speaking && !mp3_active) {
            ESP_LOGI(TAG, "Goodbye pending, disconnecting MQTT after playback finished");
            _mqtt_goodbye_pending = false;
            _protocol->getClient()->disconnect();
        } else {
            ESP_LOGI(TAG,
                     "Goodbye pending, waiting for playback to finish (speaking=%d mp3=%d)",
                     speaking ? 1 : 0, mp3_active ? 1 : 0);
        }
    }
#endif
}
