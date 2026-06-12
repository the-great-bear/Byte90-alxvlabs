/**
 * ProtocolManager.cpp
 *
 * Implementation for ProtocolManager.
 */

#include "ProtocolManager.h"

#include "ApplicationAudio.h"
#include "DeviceConfig.h"
#include "HapticsManager.h"
#include "McpServer.h"
#include "McpToolManager.h"
#include "Mp3Player.h"
#include "AudioService.h"
#include "RealtimeAiProvider.h"
#include "WebsocketClient.h"

#include <cJSON.h>
#include <esp_log.h>

static const char* TAG = "ProtocolManager";

// Provider-specific NVS getter for the active realtime AI client's API key.
#if defined(AI_PROVIDER_GEMINI)
#define REALTIME_GET_API_KEY(storage) ((storage)->getGeminiApiKey())
#else
#define REALTIME_GET_API_KEY(storage) ((storage)->getOpenAiApiKey())
#endif

ProtocolManager::ProtocolManager(
    NVSStorage* storage,
    SystemStateManager*& state_manager,
    ApplicationAudio*& audio,
    McpServer*& mcp_server,
    bool& protocol_connected,
    bool& protocol_ready,
    bool& pending_listening_start,
    McpToolManager* mcp_tool_manager
)
    : _storage(storage)
    , _state_manager(state_manager)
    , _audio(audio)
    , _mcp_server(mcp_server)
    , _protocol_connected(protocol_connected)
    , _protocol_ready(protocol_ready)
    , _pending_listening_start(pending_listening_start)
    , _mcp_tool_manager(mcp_tool_manager)
    , _openai_client(nullptr)
    , _pending_disconnect_sound(false)
    , _idle_goodbye_in_progress(false)
    , _idle_disconnect_sound_started(false)
    , _idle_goodbye_sent_ms(0)
    , _disconnect_in_progress(false)
    , _emotion_callback(nullptr)
    , _network_error_first_ms(0)
    , _network_error_count(0)
    , _audio_congestion_first_ms(0)
    , _audio_congestion_last_ms(0)
    , _mcp_tools_ready(false)
    , _openai_emotion_pending(false)
    , _openai_emotion_trigger_ms(0)
    , _openai_skip_first_emotion(true) {
}

ProtocolManager::~ProtocolManager() {
    delete _openai_client;
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

    ESP_LOGI(TAG, "9.1. OpenAI Realtime...");
    _openai_client = new RealtimeAiClient();
    if (!_openai_client->begin()) {
        ESP_LOGE(TAG, "❌ Failed to initialize realtime AI client");
        return;
    }

    if (_storage) {
        String api_key = REALTIME_GET_API_KEY(_storage);
        if (api_key.length() == 0) {
            ESP_LOGW(TAG, "OpenAI API key missing in NVS");
        }
        _openai_client->setApiKey(api_key);
    } else {
        ESP_LOGW(TAG, "OpenAI API key unavailable (storage not ready)");
    }

    if (_audio) {
        _audio->setOpenAIClient(_openai_client);
    }
    if (_mcp_server) {
        _openai_client->setMcpServer(_mcp_server);
    }

    _openai_client->onConnected([this]() {
        _protocol_connected = true;
        _protocol_ready = true;
        _mcp_tools_ready = true;

        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_CONNECTING);
        }

        if (_pending_listening_start) {
            ESP_LOGI(TAG, "✅ Connection complete, starting listening...");
            _pending_listening_start = false;
            startListening();
        }
    });

    _openai_client->onDisconnected([this]() {
        ESP_LOGW(TAG, "OpenAI realtime disconnected - starting cleanup");
        bool was_listening = _audio && _audio->isListening();
        bool play_sound = _pending_disconnect_sound || was_listening;
        _pending_disconnect_sound = false;
        performDisconnect(play_sound);
    });

    _openai_client->onNetworkError([this](const String& message) {
        handleNetworkError(message);
    });

    _openai_client->onSpeechState([this](bool speaking) {
        if (_audio) {
            _audio->handleOpenAISpeechState(speaking);
        }
        if (speaking) {
            if (_openai_skip_first_emotion) {
                _openai_skip_first_emotion = false;
                _openai_emotion_pending = false;
                _openai_emotion_trigger_ms = 0;
            } else {
                _openai_emotion_pending = true;
                _openai_emotion_trigger_ms = millis() + 300;
            }
        } else {
            _openai_emotion_pending = false;
            _openai_emotion_trigger_ms = 0;
        }
    });

    _openai_client->onBeforeResponseCreate([this]() {
        if (_audio) {
            _audio->handleOpenAIResponseStart();
        }
    });

    _openai_client->onResponseDone([this]() {
        if (_audio) {
            _audio->handleOpenAIResponseDone();
        }
    });

    _openai_client->onOutputAudioDone([this]() {
        if (_audio) {
            _audio->handleOpenAIOutputAudioDone();
        }
    });

    _openai_client->onToolCallStart([this]() {
        if (_mcp_tool_manager) {
            _mcp_tool_manager->enterToolLoading();
        }
    });

    _openai_client->onToolCallEnd([this]() {
        if (_mcp_tool_manager) {
            _mcp_tool_manager->exitToolLoading();
        }
    });

    ESP_LOGI(TAG, "✅ OpenAI realtime initialized");
}

void ProtocolManager::setMcpServer(McpServer* server) {
    _mcp_server = server;
    if (_openai_client) {
        _openai_client->setMcpServer(server);
    }
}

void ProtocolManager::setEmotionCallback(std::function<void(const String&)> callback) {
    _emotion_callback = callback;
}

void ProtocolManager::setConfigRefreshCallback(std::function<void()> callback) {
    _config_refresh_callback = callback;
}

void ProtocolManager::connectProtocol() {
    if (!_openai_client) {
        ESP_LOGE(TAG, "OpenAI client not initialized");
        return;
    }

    if (_state_manager) {
        _state_manager->setState(SYSTEM_STATE_CONNECTING);
    }
    if (!_openai_client->hasApiKey() && _storage) {
        String api_key = REALTIME_GET_API_KEY(_storage);
        if (api_key.length() > 0) {
            _openai_client->setApiKey(api_key);
        }
    }
    if (!_openai_client->hasApiKey()) {
        ESP_LOGW(TAG, "Realtime AI API key missing, aborting connect");
        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_IDLE);
        }
        return;
    }
    _openai_client->connect();
}

void ProtocolManager::handleIncomingJson(const cJSON* root) {
    (void)root;
}

void ProtocolManager::handleIncomingAudio(AudioStreamPacket* packet) {
    if (packet) {
        delete packet;
    }
}

void ProtocolManager::getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const {
    ws_client = nullptr;
    hello_received = false;
}

WebSocketClient* ProtocolManager::getWebsocketClient() const {
    return nullptr;
}

void ProtocolManager::poll() {
    if (_openai_client && !_openai_client->isWorkerRunning()) {
        _openai_client->poll();
    }
}

void ProtocolManager::startListening() {
    if (!_audio) return;

    _audio->startListening(_state_manager);
}

void ProtocolManager::stopListening() {
    if (!_audio || !_audio->isListening()) {
        return;
    }

    ESP_LOGI(TAG, "User stopped listening, sending abort and disconnecting");
    _openai_emotion_pending = false;
    _openai_emotion_trigger_ms = 0;
    _openai_skip_first_emotion = true;
    _idle_goodbye_in_progress = false;
    _idle_disconnect_sound_started = false;
    _idle_goodbye_sent_ms = 0;

    _audio->stopListening(_state_manager);
    _pending_disconnect_sound = true;
    if (_openai_client) {
        _openai_client->cancelResponse();
        _openai_client->clearInputAudioBuffer();
        if (_audio && _audio->getService() && _audio->getService()->isPcmPlaybackActive()) {
            _audio->getService()->stopPcmPlayback();
        }
        _openai_client->disconnect();
    }
}

void ProtocolManager::abortResponse(const String& reason) {
    (void)reason;
}

void ProtocolManager::cancelOpenAIResponse() {
    if (_audio) {
        _audio->interruptOpenAIResponse();
    } else if (_openai_client) {
        _openai_client->cancelResponse();
        _openai_client->clearPlaybackBuffer();
        _openai_client->clearInputAudioBuffer();
    }
}

void ProtocolManager::performDisconnect(bool playSound) {
    if (_disconnect_in_progress) {
        return;
    }
    _disconnect_in_progress = true;
    _openai_emotion_pending = false;
    _openai_emotion_trigger_ms = 0;
    _openai_skip_first_emotion = true;

    if (_audio) {
        _audio->resetListeningState();
        if (_audio->getService()) {
            _audio->getService()->stopCapture();
            if (_audio->getService()->isPcmPlaybackActive()) {
                _audio->getService()->stopPcmPlayback();
            }
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

    if (_openai_emotion_pending && _emotion_callback &&
        _openai_emotion_trigger_ms > 0 &&
        (int32_t)(millis() - _openai_emotion_trigger_ms) >= 0) {
        _openai_emotion_pending = false;
        _openai_emotion_trigger_ms = 0;
        _emotion_callback("random");
    }

    if (_idle_goodbye_in_progress) {
        if (_audio) {
            _audio->updateOpenAI();
        }
        if (_idle_disconnect_sound_started) {
            Mp3Player* mp3 = _audio ? _audio->getMp3Player() : nullptr;
            if (!mp3 || !mp3->isPlaying()) {
                _pending_disconnect_sound = false;
                _idle_goodbye_in_progress = false;
                _idle_disconnect_sound_started = false;
                _idle_goodbye_sent_ms = 0;
                if (_openai_client) {
                    if (_audio && _audio->getService() && _audio->getService()->isPcmPlaybackActive()) {
                        _audio->getService()->stopPcmPlayback();
                    }
                    _openai_client->disconnect();
                }
            }
            return;
        }

        if (_audio && _audio->isOpenAIOutputDrained()) {
            _idle_disconnect_sound_started = true;
            _pending_disconnect_sound = false;
            if (_audio->getService() && _audio->getService()->isPcmPlaybackActive()) {
                _audio->getService()->stopPcmPlayback();
            }
            _audio->playSoundWithHaptic("/sounds/disconnect.mp3",
                                        HapticsManager::HAPTIC_EVENT_DISCONNECT);
            return;
        }
        return;
    }

    // Idle timeout: after REALTIME_IDLE_DISCONNECT_MS without user speech while
    // listening, have the assistant say goodbye and then tear the session down.
    if (_openai_client && _audio->isListening() && _openai_client->isConnected()) {
        uint32_t last_speech_ms = _openai_client->lastUserSpeechMs();
        if (last_speech_ms > 0 &&
            REALTIME_IDLE_DISCONNECT_MS > 0 &&
            millis() - last_speech_ms >= REALTIME_IDLE_DISCONNECT_MS) {
            ESP_LOGI(TAG, "Idle timeout reached (%u ms), sending goodbye",
                     REALTIME_IDLE_DISCONNECT_MS);
            _idle_goodbye_in_progress = true;
            _idle_disconnect_sound_started = false;
            _idle_goodbye_sent_ms = millis();
            if (_audio && _audio->getService() && _audio->getService()->isCaptureActive()) {
                _audio->getService()->stopCapture();
            }
            if (_audio) {
                _audio->resetListeningState();
            }
            if (_openai_client) {
                _openai_client->clearInputAudioBuffer();
                if (!_openai_client->sendTextResponse(REALTIME_IDLE_GOODBYE_TEXT)) {
                    ESP_LOGW(TAG, "Failed to send idle goodbye, disconnecting");
                    _idle_disconnect_sound_started = true;
                    _pending_disconnect_sound = false;
                    _idle_goodbye_sent_ms = 0;
                    if (_audio) {
                        if (_audio->getService() && _audio->getService()->isPcmPlaybackActive()) {
                            _audio->getService()->stopPcmPlayback();
                        }
                        _audio->playSoundWithHaptic("/sounds/disconnect.mp3",
                                                    HapticsManager::HAPTIC_EVENT_DISCONNECT);
                    }
                }
            }
            return;
        }
    }
    _audio->updateOpenAI();
}
