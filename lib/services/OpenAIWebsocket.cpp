/**
 * OpenAIWebsocket.cpp
 *
 * Implementation for OpenAIWebsocket.
 */

#include "OpenAIWebsocket.h"
#include "OpenAIApiClient.h"
#include "certs/OpenAICert.h"

#include "DeviceConfig.h"
#include "TaskManager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "OpenAIWebsocket";

namespace {
constexpr size_t OPENAI_PCM_RING_BYTES = 2048 * 1024;  // 2MB = ~43.7 seconds at 24kHz
constexpr size_t OPENAI_PCM_BYTES_PER_SAMPLE = 2;
constexpr size_t OPENAI_PCM_CHUNK_SAMPLES = 240; // 10 ms at 24 kHz
constexpr size_t OPENAI_TX_RING_BYTES = 32 * 1024;
constexpr size_t OPENAI_TOOL_QUEUE_DEPTH = 4;
constexpr uint32_t OPENAI_INPUT_CLEAR_COOLDOWN_MS = 500;
static uint32_t openai_session_counter = 0;
} // namespace

struct ToolCallItem {
    String call_id;
    String name;
    String args;
};

struct ToolResultItem {
    String call_id;
    String result;
};

OpenAIWebsocket::OpenAIWebsocket()
    : _api(nullptr)
    , _connected(false)
    , _session_ready(false)
    , _auto_reconnect(false)
    , _speaking(false)
    , _response_in_progress(false)
    , _sent_startup_text(false)
    , _min_speech_ms(500)
    , _last_speech_start_ms(-1)
    , _last_user_speech_ms(0)
    , _last_input_clear_ms(0)
    , _api_key("")
    , _ring(nullptr)
    , _ring_size(OPENAI_PCM_RING_BYTES)
    , _head(0)
    , _tail(0)
    , _ring_mux(portMUX_INITIALIZER_UNLOCKED)
    , _tx_ring(nullptr)
    , _tx_ring_size(OPENAI_TX_RING_BYTES)
    , _tx_head(0)
    , _tx_tail(0)
    , _tx_mux(portMUX_INITIALIZER_UNLOCKED)
    , _worker_running(false)
    , _tool_call_queue(nullptr)
    , _tool_result_queue(nullptr)
    , _tool_worker_running(false)
    , _mcp_server(nullptr)
    , _ai_adapter(nullptr)
    , _vad_threshold(0.3f)           // Moderate sensitivity (0.0-1.0)
    , _vad_prefix_padding_ms(600)    // 600ms before speech start (captures beginning of words)
    , _vad_silence_duration_ms(500)  // 500ms silence before turn end
{
    _api = new OpenAIApiClient(&_ws);
}

OpenAIWebsocket::~OpenAIWebsocket() {
    stopWorker();
    _tool_worker_running = false;
    TaskManager::instance().stopTask("openai_tool");
    if (_tool_call_queue) {
        vQueueDelete(_tool_call_queue);
        _tool_call_queue = nullptr;
    }
    if (_tool_result_queue) {
        vQueueDelete(_tool_result_queue);
        _tool_result_queue = nullptr;
    }
    disconnect();
    if (_ring) {
        heap_caps_free(_ring);
        _ring = nullptr;
    }
    if (_tx_ring) {
        heap_caps_free(_tx_ring);
        _tx_ring = nullptr;
    }
    if (_api) {
        delete _api;
        _api = nullptr;
    }
    if (_ai_adapter) {
        delete _ai_adapter;
        _ai_adapter = nullptr;
    }
}

bool OpenAIWebsocket::begin() {
    if (_ring != nullptr) {
        return true;
    }

    // Check available SPIRAM before allocation
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_needed = _ring_size + _tx_ring_size;
    ESP_LOGI(TAG, "SPIRAM status: %u KB free, need %u KB for buffers",
             free_spiram / 1024, total_needed / 1024);

    if (free_spiram < total_needed) {
        ESP_LOGE(TAG, "Insufficient SPIRAM! Need %u KB, only %u KB available",
                 total_needed / 1024, free_spiram / 1024);
        return false;
    }

    _ring = static_cast<uint8_t*>(heap_caps_malloc(_ring_size, MALLOC_CAP_SPIRAM));
    if (!_ring) {
        ESP_LOGE(TAG, "Failed to allocate PCM ring buffer (%u bytes)", _ring_size);
        return false;
    }

    _tx_ring = static_cast<uint8_t*>(heap_caps_malloc(_tx_ring_size, MALLOC_CAP_SPIRAM));
    if (!_tx_ring) {
        ESP_LOGE(TAG, "Failed to allocate TX ring buffer (%u bytes)", _tx_ring_size);
        heap_caps_free(_ring);
        _ring = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Successfully allocated %u KB for audio buffers (RX: %u KB, TX: %u KB)",
             total_needed / 1024, _ring_size / 1024, _tx_ring_size / 1024);

    // Report PSRAM usage after OpenAI buffer allocation
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_used = psram_total - psram_free;
    ESP_LOGI(TAG, "PSRAM after OpenAI buffers: %u KB used / %u KB total (%.1f%% free)",
             psram_used / 1024, psram_total / 1024, (psram_free * 100.0f) / psram_total);

    _ws.onEvent([this](WSTYPE_t type, uint8_t* payload, size_t length) {
        handleEvent(type, payload, length);
    });

    if (!_tool_call_queue) {
        _tool_call_queue = xQueueCreate(OPENAI_TOOL_QUEUE_DEPTH, sizeof(ToolCallItem*));
    }
    if (!_tool_result_queue) {
        _tool_result_queue = xQueueCreate(OPENAI_TOOL_QUEUE_DEPTH, sizeof(ToolResultItem*));
    }
    if (_tool_call_queue && _tool_result_queue && !_tool_worker_running) {
        _tool_worker_running = true;
        bool created = TaskManager::instance().createTask(
            "openai_tool",
            "OpenAIWebsocket",
            toolWorkerTask,
            this,
            3,                      // Priority
            0,                      // Core 0
            8192,                   // 8KB stack
            CleanupPattern::FORCE_DELETE,
            "OpenAI MCP tool call processing"
        );
        if (!created) {
            _tool_worker_running = false;
            ESP_LOGE(TAG, "Failed to start OpenAI tool worker");
        }
    }

    return startWorker();
}

void OpenAIWebsocket::connect() {
    startWorker();
    if (_api_key.isEmpty()) {
        ESP_LOGE(TAG, "OpenAI API key missing, cannot connect");
        _auto_reconnect = false;
        return;
    }

    _auto_reconnect = true;
    String headers = "Authorization: Bearer ";
    headers += _api_key;
    _ws.setExtraHeaders(headers.c_str());
    _ws.setReconnectInterval(1000);
    _ws.setMaxReconnectAttempts(PROTOCOL_MAX_RECONNECT_ATTEMPTS);

    String path = String(OPENAI_REALTIME_PATH) + OPENAI_REALTIME_MODEL;
    _ws.beginSSL(OPENAI_REALTIME_HOST,
                 OPENAI_REALTIME_PORT,
                 path.c_str(),
                 ROOT_CA_CERTIFICATE,
                 "openai-realtime");
}

void OpenAIWebsocket::disconnect() {
    _auto_reconnect = false;
    stopWorker();
    _ws.disconnect();
    _connected = false;
    _session_ready = false;
    _response_in_progress = false;
    _sent_startup_text = false;
    _last_user_speech_ms = 0;
    _current_response_id = "";
    updateSpeaking(false);
}

void OpenAIWebsocket::poll() {
    if (_worker_running) {
        return;
    }
    if (!_auto_reconnect && !_connected) {
        return;
    }
    _ws.loop();
    drainTxQueue();
}

void OpenAIWebsocket::onNetworkError(std::function<void(const String&)> callback) {
    _on_network_error = callback;
}

bool OpenAIWebsocket::sendPcm(const int16_t* samples, size_t sample_count) {
    static uint32_t last_failure_log = 0;
    static int failure_count = 0;

    if (!_connected || !_session_ready || !samples || sample_count == 0 || !_api) {
        // Log every 100 failures or every 2 seconds
        if (++failure_count % 100 == 0 || millis() - last_failure_log > 2000) {
            ESP_LOGW(TAG, "🔴 sendPcm blocked: connected=%d session_ready=%d samples=%p count=%u api=%p (failed %d times)",
                     _connected, _session_ready, samples, sample_count, _api, failure_count);
            last_failure_log = millis();
            failure_count = 0;
        }
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
    size_t byte_count = sample_count * OPENAI_PCM_BYTES_PER_SAMPLE;
    String b64 = base64Encode(bytes, byte_count);

    bool result = _api->sendInputAudioAppend(b64);
    if (!result) {
        if (++failure_count % 100 == 0 || millis() - last_failure_log > 2000) {
            ESP_LOGE(TAG, "🔴 sendInputAudioAppend FAILED (size: %u bytes, failed %d times)", byte_count, failure_count);
            last_failure_log = millis();
        }
        if (_on_network_error) {
            _on_network_error("OpenAI sendInputAudioAppend failed");
        }
    } else {
        failure_count = 0;  // Reset on success
    }

    return result;
}

bool OpenAIWebsocket::enqueuePcm(const int16_t* samples, size_t sample_count) {
    if (!samples || sample_count == 0 || !_tx_ring) {
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
    size_t byte_count = sample_count * OPENAI_PCM_BYTES_PER_SAMPLE;

    portENTER_CRITICAL(&_tx_mux);
    size_t free_bytes = (_tx_tail + _tx_ring_size - _tx_head - 1) % _tx_ring_size;
    if (byte_count > free_bytes) {
        portEXIT_CRITICAL(&_tx_mux);
        return false;
    }

    size_t first = min(byte_count, _tx_ring_size - _tx_head);
    memcpy(_tx_ring + _tx_head, bytes, first);
    memcpy(_tx_ring, bytes + first, byte_count - first);
    _tx_head = (_tx_head + byte_count) % _tx_ring_size;
    portEXIT_CRITICAL(&_tx_mux);
    return true;
}

size_t OpenAIWebsocket::popPcm(int16_t* out, size_t max_samples) {
    if (!out || max_samples == 0) {
        return 0;
    }

    size_t max_bytes = max_samples * OPENAI_PCM_BYTES_PER_SAMPLE;
    size_t bytes_read = popPcmBytes(reinterpret_cast<uint8_t*>(out), max_bytes);
    return bytes_read / OPENAI_PCM_BYTES_PER_SAMPLE;
}

void OpenAIWebsocket::onConnected(std::function<void()> callback) {
    _on_connected = callback;
}

void OpenAIWebsocket::onDisconnected(std::function<void()> callback) {
    _on_disconnected = callback;
}

void OpenAIWebsocket::setMcpServer(McpServer* server) {
    _mcp_server = server;
    if (_ai_adapter) {
        delete _ai_adapter;
        _ai_adapter = nullptr;
    }
    if (_mcp_server) {
        _ai_adapter = new OpenAiAdapter(_mcp_server);
    }
}

void OpenAIWebsocket::onSpeechState(std::function<void(bool)> callback) {
    _on_speech_state = callback;
}

void OpenAIWebsocket::onBeforeResponseCreate(std::function<void()> callback) {
    _on_before_response_create = callback;
}

void OpenAIWebsocket::onResponseDone(std::function<void()> callback) {
    _on_response_done = callback;
}

void OpenAIWebsocket::onOutputAudioDone(std::function<void()> callback) {
    _on_output_audio_done = callback;
}

void OpenAIWebsocket::onToolCallStart(std::function<void()> callback) {
    _on_tool_call_start = callback;
}

void OpenAIWebsocket::onToolCallEnd(std::function<void()> callback) {
    _on_tool_call_end = callback;
}

void OpenAIWebsocket::handleEvent(WSTYPE_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WSTYPE_CONNECTED:
            _connected = true;
            _session_ready = false;
            if (_on_connected) {
                _on_connected();
            }
            break;
        case WSTYPE_ERROR:
            if (_on_network_error) {
                if (payload && length > 0) {
                    _on_network_error(String(reinterpret_cast<const char*>(payload), length));
                } else {
                    _on_network_error("openai_ws_error");
                }
            }
            break;
        case WSTYPE_DISCONNECTED:
            _connected = false;
            _session_ready = false;
            _sent_startup_text = false;
            if (_on_disconnected) {
                _on_disconnected();
            }
            break;
        case WSTYPE_TEXT:
            if (payload && length > 0) {
                handleText(reinterpret_cast<const char*>(payload), length);
            }
            break;
        default:
            break;
    }
}

void OpenAIWebsocket::handleText(const char* data, size_t length) {
    if (!data || length == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, length);
    if (error) {
        constexpr size_t PREVIEW_LEN = 200;
        size_t preview_len = min(length, PREVIEW_LEN);
        char preview[PREVIEW_LEN + 1];
        memcpy(preview, data, preview_len);
        preview[preview_len] = '\0';
        ESP_LOGW(TAG,
                 "Failed to parse message: %s (len=%u, preview=\"%s\")",
                 error.c_str(),
                 static_cast<unsigned int>(length),
                 preview);
        return;
    }

    const char* type = doc["type"];
    if (!type) {
        return;
    }

    if (strcmp(type, "session.created") == 0) {
        ESP_LOGI(TAG, "[Session Created]");
        if (_mcp_server) {
            openai_session_counter++;
            _mcp_server->setSessionId(String("openai-") + String(openai_session_counter));
        }
        sendSessionUpdate();
    } else if (strcmp(type, "session.updated") == 0) {
        ESP_LOGI(TAG, "[Session Updated]");
        _session_ready = true;
        _last_user_speech_ms = millis();
        if (!_sent_startup_text && OPENAI_STARTUP_TEXT[0] != '\0' && _api) {
            _api->sendConversationItemCreate(OPENAI_STARTUP_TEXT);
            sendResponseCreate();
            _sent_startup_text = true;
        }
    } else if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
        const char* event_id = doc["event_id"] | "";
        const char* item_id = doc["item_id"] | "";
        int audio_start_ms = doc["audio_start_ms"] | -1;
        _last_user_speech_ms = millis();
        if (_speaking) {
            ESP_LOGI(TAG, "[VAD] speech_started ignored (speaking) event_id=%s", event_id);
            sendInputAudioClear();
            _last_speech_start_ms = -1;
            return;
        }
        _last_speech_start_ms = audio_start_ms;
        ESP_LOGI(TAG, "[VAD] speech_started event_id=%s item_id=%s start_ms=%d", event_id, item_id, audio_start_ms);
    } else if (strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        const char* event_id = doc["event_id"] | "";
        const char* item_id = doc["item_id"] | "";
        int audio_end_ms = doc["audio_end_ms"] | -1;
        if (_speaking || _response_in_progress) {
            ESP_LOGI(TAG, "[VAD] speech_stopped ignored (speaking/response) event_id=%s", event_id);
            sendInputAudioClear();
            _last_speech_start_ms = -1;
            return;
        }
        ESP_LOGI(TAG, "[VAD] speech_stopped event_id=%s item_id=%s end_ms=%d", event_id, item_id, audio_end_ms);
        int duration_ms = (audio_end_ms >= 0 && _last_speech_start_ms >= 0)
                              ? (audio_end_ms - _last_speech_start_ms)
                              : -1;
        if (duration_ms < _min_speech_ms) {
            ESP_LOGI(TAG, "[VAD] Ignoring short segment (%d ms)", duration_ms);
            // Clear buffer for short segments too - don't let them accumulate
            sendInputAudioClear();
        }
        _last_speech_start_ms = -1;
    } else if (strcmp(type, "input_audio_buffer.committed") == 0) {
        const char* item_id = doc["item_id"] | "";
        const char* previous_item_id = doc["previous_item_id"] | "";
        ESP_LOGI(TAG, "[Audio Commit] item_id=%s prev=%s", item_id, previous_item_id);
    } else if (strcmp(type, "error") == 0) {
        JsonObject error = doc["error"].as<JsonObject>();
        const char* code = error["code"] | "";
        const char* message = error["message"] | "";
        const char* event_id = error["event_id"] | "";
        ESP_LOGW(TAG, "[Error] code=%s event_id=%s message=%s", code, event_id, message);
    } else if (strcmp(type, "response.created") == 0) {
        const char* event_id = doc["event_id"] | "";
        JsonObject response = doc["response"].as<JsonObject>();
        const char* response_id = response["id"] | "";
        const char* status = response["status"] | "";
        ESP_LOGI(TAG, "[Response Created] event_id=%s id=%s status=%s", event_id, response_id, status);
        _current_response_id = String(response_id);
        _last_user_speech_ms = millis();

        // Immediately stop capturing to prevent echo
        updateSpeaking(true);
    } else if (strcmp(type, "response.done") == 0) {
        const char* event_id = doc["event_id"] | "";
        JsonObject response = doc["response"].as<JsonObject>();
        const char* status = response["status"] | "";
        const char* response_id = response["id"] | "";
        _last_response_id = String(response_id);  // Store for later cancellation

        // Extract transcript from output items
        const char* transcript = "";
        JsonArray output = response["output"].as<JsonArray>();
        if (output.size() > 0) {
            JsonObject first_item = output[0].as<JsonObject>();
            JsonArray content = first_item["content"].as<JsonArray>();
            if (content.size() > 0) {
                JsonObject first_content = content[0].as<JsonObject>();
                transcript = first_content["transcript"] | "";
            }
        }

        // Extract usage statistics
        JsonObject usage = response["usage"].as<JsonObject>();
        int total_tokens = usage["total_tokens"] | 0;
        int input_tokens = usage["input_tokens"] | 0;
        int output_tokens = usage["output_tokens"] | 0;

        ESP_LOGI(TAG, "[Response Done] event_id=%s id=%s status=%s tokens=%d/%d/%d",
                 event_id, response_id, status, input_tokens, output_tokens, total_tokens);
        if (transcript && transcript[0] != '\0') {
            ESP_LOGI(TAG, "[Response Transcript] \"%s\"", transcript);
        }

        _last_user_speech_ms = millis();
        _response_in_progress = false;
        _current_response_id = "";
        updateSpeaking(false);
        if (_on_response_done) {
            _on_response_done();
        }
    } else if (strcmp(type, "response.output_text.delta") == 0) {
        const char* delta = doc["delta"];
        if (delta && delta[0] != '\0') {
            ESP_LOGI(TAG, "[Text] %s", delta);
        }
    } else if (strcmp(type, "response.output_text.done") == 0) {
        const char* text = doc["text"];
        if (text && text[0] != '\0') {
            ESP_LOGI(TAG, "[Text Done] %s", text);
        }
    } else if (strcmp(type, "response.function_call_arguments.done") == 0) {
        const char* call_id = doc["call_id"] | "";
        const char* name = doc["name"] | "";
        const char* args = doc["arguments"] | "";

        ESP_LOGI(TAG, "[Tool Call] ID: %s Func: %s Args: %s", call_id, name, args);

        if (_tool_call_queue) {
            ToolCallItem* item = new ToolCallItem{
                String(call_id),
                String(name),
                String(args)
            };
            if (xQueueSend(_tool_call_queue, &item, 0) != pdTRUE) {
                delete item;
                ESP_LOGW(TAG, "Tool queue full, dropping tool call");
                if (_api) {
                    String err = "{\"error\":\"tool queue full\"}";
                    _api->sendFunctionCallOutput(call_id, err);
                    _api->sendResponseCreate();
                }
            } else if (_on_tool_call_start) {
                _on_tool_call_start();
            }
        }
    } else if (strcmp(type, "response.output_audio.delta") == 0) {
        updateSpeaking(true);
        _last_user_speech_ms = millis();
        const char* delta = doc["delta"];
        if (delta && delta[0] != '\0') {
            size_t input_len = strlen(delta);
            size_t decoded_max = (input_len * 3) / 4 + 4;
            uint8_t stack_buffer[4096];
            uint8_t* decode_buffer = stack_buffer;
            size_t buffer_size = sizeof(stack_buffer);

            if (decoded_max > buffer_size) {
                decode_buffer = static_cast<uint8_t*>(
                    heap_caps_malloc(decoded_max, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                buffer_size = decoded_max;
            }

            if (decode_buffer) {
                size_t decoded = base64Decode(delta, decode_buffer, buffer_size);
                if (decoded > 0) {
                    pushPcmBytes(decode_buffer, decoded);
                }
            }

            if (decode_buffer != stack_buffer) {
                heap_caps_free(decode_buffer);
            }
        }
    } else if (strcmp(type, "response.output_audio.done") == 0) {
        ESP_LOGI(TAG, "[Audio Done]");
        updateSpeaking(false);
        _last_user_speech_ms = millis();
        if (_on_output_audio_done) {
            _on_output_audio_done();
        }
    } else if (strcmp(type, "input_audio_buffer.cleared") == 0) {
        const char* event_id = doc["event_id"] | "";
        ESP_LOGI(TAG, "[Buffer Cleared] event_id=%s", event_id);
    } else if (strcmp(type, "response.cancelled") == 0) {
        const char* response_id = doc["response_id"] | "";
        ESP_LOGI(TAG, "[Response Cancelled] response_id=%s", response_id);
        if (_current_response_id == String(response_id)) {
            _current_response_id = "";
        }
    } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
        const char* item_id = doc["item_id"] | "";
        const char* transcript = doc["transcript"] | "";
        int content_index = doc["content_index"] | 0;
        ESP_LOGI(TAG, "[Transcription] item_id=%s idx=%d text=\"%s\"", item_id, content_index, transcript);
    }
}

void OpenAIWebsocket::sendSessionUpdate() {
    if (!_api) {
        return;
    }

    JsonDocument doc;
    _api->buildSessionUpdate(
        doc,
        OPENAI_REALTIME_MODEL,
        OPENAI_REALTIME_VOICE,
        OPENAI_REALTIME_INSTRUCTIONS,
        _vad_threshold,           // Use configured VAD threshold
        _vad_prefix_padding_ms,   // Use configured prefix padding
        _vad_silence_duration_ms  // Use configured silence duration
    );

    ESP_LOGI(TAG, "Session update VAD config: threshold=%.2f, prefix_padding=%dms, silence_duration=%dms",
             _vad_threshold, _vad_prefix_padding_ms, _vad_silence_duration_ms);
    JsonArray tools_array = doc["session"]["tools"].to<JsonArray>();
    bool tools_added = tools_array.size() > 0;
    if (_ai_adapter) {
        _ai_adapter->generateSchema(tools_array);
        tools_added = tools_array.size() > 0;
    }
    if (tools_added) {
        doc["session"]["tool_choice"] = "auto";
    }

    _api->sendSessionUpdate(doc);
}

bool OpenAIWebsocket::sendTextResponse(const String& text) {
    if (!_api || !_connected || !_session_ready || _response_in_progress) {
        return false;
    }
    if (text.length() == 0) {
        return false;
    }
    if (!_api->sendConversationItemCreate(text)) {
        if (_on_network_error) {
            _on_network_error("OpenAI sendConversationItemCreate failed");
        }
        return false;
    }
    sendResponseCreate();
    return true;
}

void OpenAIWebsocket::sendResponseCreate() {
    if (_response_in_progress || !_api) {
        return;
    }
    if (_on_before_response_create) {
        _on_before_response_create();
    }
    _response_in_progress = true;
    if (!_api->sendResponseCreate()) {
        if (_on_network_error) {
            _on_network_error("OpenAI sendResponseCreate failed");
        }
    }
}

void OpenAIWebsocket::sendInputAudioClear() {
    if (!_api) {
        return;
    }
    uint32_t now = millis();
    if ((now - _last_input_clear_ms) < OPENAI_INPUT_CLEAR_COOLDOWN_MS) {
        ESP_LOGI(TAG, "Skipping input_audio_buffer.clear (cooldown)");
        return;
    }
    if (!_api->sendInputAudioClear()) {
        if (_on_network_error) {
            _on_network_error("OpenAI sendInputAudioClear failed");
        }
    }
    ESP_LOGI(TAG, "Sent input_audio_buffer.clear");
    _last_input_clear_ms = now;
}

void OpenAIWebsocket::sendResponseCancel(const String& response_id) {
    if (!_api) {
        return;
    }
    if (!_api->sendResponseCancel(response_id)) {
        if (_on_network_error) {
            _on_network_error("OpenAI sendResponseCancel failed");
        }
    }
    ESP_LOGI(TAG, "Sent response.cancel (response_id=%s)", response_id.c_str());
}

void OpenAIWebsocket::clearTxRing() {
    if (!_tx_ring) {
        return;
    }

    portENTER_CRITICAL(&_tx_mux);
    _tx_head = _tx_tail;
    portEXIT_CRITICAL(&_tx_mux);
}

void OpenAIWebsocket::updateSpeaking(bool speaking) {
    if (_speaking == speaking) {
        return;
    }
    ESP_LOGI(TAG, "[Speaking State] %s -> %s", _speaking ? "true" : "false", speaking ? "true" : "false");
    _speaking = speaking;
    if (speaking) {
        clearTxRing();
        sendInputAudioClear();
    } else {
        clearTxRing();
        _last_speech_start_ms = -1;
    }
    if (_on_speech_state) {
        _on_speech_state(speaking);
    }
}

void OpenAIWebsocket::workerTask(void* parameter) {
    OpenAIWebsocket* client = static_cast<OpenAIWebsocket*>(parameter);
    if (client) {
        client->workerLoop();
    }
    TaskManager::instance().markTaskStopped("openai_ws");
    vTaskDelete(nullptr);
}

void OpenAIWebsocket::workerLoop() {
    while (_worker_running) {
        if (!_auto_reconnect && !_connected) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        _ws.loop();
        processToolResults();
        drainTxQueue();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void OpenAIWebsocket::toolWorkerTask(void* parameter) {
    OpenAIWebsocket* client = static_cast<OpenAIWebsocket*>(parameter);
    if (client) {
        client->toolWorkerLoop();
    }
    TaskManager::instance().markTaskStopped("openai_tool");
    vTaskDelete(nullptr);
}

void OpenAIWebsocket::toolWorkerLoop() {
    while (_tool_worker_running) {
        ToolCallItem* item = nullptr;
        if (_tool_call_queue &&
            xQueueReceive(_tool_call_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (!item) {
                continue;
            }

            String result = "{\"error\":\"tool adapter not available\"}";
            if (_ai_adapter) {
                result = _ai_adapter->executeTool(item->name, item->args);
            }

            ToolResultItem* out = new ToolResultItem{item->call_id, result};
            if (!_tool_result_queue ||
                xQueueSend(_tool_result_queue, &out, 0) != pdTRUE) {
                delete out;
                ESP_LOGW(TAG, "Tool result queue full, dropping result");
            }

            delete item;
        }
    }
}

void OpenAIWebsocket::processToolResults() {
    if (!_tool_result_queue || !_api) {
        return;
    }

    ToolResultItem* result = nullptr;
    while (xQueueReceive(_tool_result_queue, &result, 0) == pdTRUE) {
        if (!result) {
            continue;
        }
        ESP_LOGI(TAG, "[Tool Result] %s", result->result.c_str());
        _api->sendFunctionCallOutput(result->call_id, result->result);
        _api->sendResponseCreate();
        if (_on_tool_call_end) {
            _on_tool_call_end();
        }
        delete result;
    }
}

void OpenAIWebsocket::drainTxQueue() {
    static uint32_t last_warning = 0;
    static size_t last_used = 0;

    if (!_connected || !_session_ready || !_tx_ring) {
        // Log why TX queue can't drain
        portENTER_CRITICAL(&_tx_mux);
        size_t used = (_tx_head + _tx_ring_size - _tx_tail) % _tx_ring_size;
        portEXIT_CRITICAL(&_tx_mux);

        if (used > _tx_ring_size / 2 && millis() - last_warning > 2000) {
            ESP_LOGW(TAG, "🟡 TX queue blocked: connected=%d session_ready=%d used=%u/%u bytes (%.1f%%)",
                     _connected, _session_ready, used, _tx_ring_size, (used * 100.0f) / _tx_ring_size);
            last_warning = millis();
        }
        return;
    }

    uint8_t buffer[OPENAI_PCM_CHUNK_SAMPLES * OPENAI_PCM_BYTES_PER_SAMPLE];
    size_t max_bytes = sizeof(buffer);

    for (;;) {
        portENTER_CRITICAL(&_tx_mux);
        size_t used = (_tx_head + _tx_ring_size - _tx_tail) % _tx_ring_size;
        if (used == 0) {
            portEXIT_CRITICAL(&_tx_mux);
            break;
        }

        // Log if buffer is getting full
        if (used > last_used + 5000 || (used > _tx_ring_size * 0.8f && millis() - last_warning > 2000)) {
            portEXIT_CRITICAL(&_tx_mux);
            ESP_LOGW(TAG, "🟠 TX queue filling: %u/%u bytes (%.1f%%)", used, _tx_ring_size, (used * 100.0f) / _tx_ring_size);
            last_warning = millis();
            last_used = used;
            portENTER_CRITICAL(&_tx_mux);
        }

        size_t take = min(used, max_bytes);
        size_t first = min(take, _tx_ring_size - _tx_tail);
        memcpy(buffer, _tx_ring + _tx_tail, first);
        memcpy(buffer + first, _tx_ring, take - first);
        _tx_tail = (_tx_tail + take) % _tx_ring_size;
        portEXIT_CRITICAL(&_tx_mux);

        const int16_t* samples = reinterpret_cast<const int16_t*>(buffer);
        size_t sample_count = take / OPENAI_PCM_BYTES_PER_SAMPLE;
        if (!sendPcm(samples, sample_count)) {
            ESP_LOGW(TAG, "🔴 drainTxQueue: sendPcm failed, breaking drain loop (queue used: %u bytes)", used);
            break;
        }

        // Log successful drain periodically (every 500 chunks or 5 seconds)
        static int drain_count = 0;
        static uint32_t last_drain_log = 0;
        if (++drain_count % 500 == 0 || millis() - last_drain_log > 5000) {
            ESP_LOGI(TAG, "✅ WebSocket TX draining (sent %d chunks, queue: %u bytes / %.1f%%)",
                     drain_count, used, (used * 100.0f) / _tx_ring_size);
            last_drain_log = millis();
            drain_count = 0;
        }
    }
}

bool OpenAIWebsocket::pushPcmBytes(const uint8_t* data, size_t length) {
    if (!data || length == 0 || !_ring) {
        return false;
    }

    portENTER_CRITICAL(&_ring_mux);
    size_t free_bytes = (_tail + _ring_size - _head - 1) % _ring_size;
    size_t used_bytes = (_head + _ring_size - _tail) % _ring_size;
    if (length > free_bytes) {
        portEXIT_CRITICAL(&_ring_mux);
        // Log buffer overflow (rate limited to avoid spam)
        static uint32_t last_overflow_log = 0;
        uint32_t now = millis();
        if (now - last_overflow_log > 1000) {
            ESP_LOGW(TAG, "PCM ring buffer full! Dropping %u bytes (used: %u/%u, %.1f%%)",
                     length, used_bytes, _ring_size, (used_bytes * 100.0f) / _ring_size);
            last_overflow_log = now;
        }
        return false;
    }

    size_t first = min(length, _ring_size - _head);
    memcpy(_ring + _head, data, first);
    memcpy(_ring, data + first, length - first);
    _head = (_head + length) % _ring_size;
    portEXIT_CRITICAL(&_ring_mux);
    return true;
}

size_t OpenAIWebsocket::popPcmBytes(uint8_t* out, size_t max_bytes) {
    if (!out || max_bytes == 0 || !_ring) {
        return 0;
    }

    portENTER_CRITICAL(&_ring_mux);
    size_t used = (_head + _ring_size - _tail) % _ring_size;
    if (used == 0) {
        portEXIT_CRITICAL(&_ring_mux);
        return 0;
    }

    size_t take = min(used, max_bytes);
    size_t first = min(take, _ring_size - _tail);
    memcpy(out, _ring + _tail, first);
    memcpy(out + first, _ring, take - first);
    _tail = (_tail + take) % _ring_size;
    portEXIT_CRITICAL(&_ring_mux);
    return take;
}

String OpenAIWebsocket::base64Encode(const uint8_t* data, size_t length) {
    size_t out_len = ((length + 2) / 3) * 4 + 1;
    char* buffer = static_cast<char*>(alloca(out_len));
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<uint8_t*>(buffer), out_len, &written, data, length) != 0) {
        return String();
    }
    buffer[written] = '\0';
    return String(buffer);
}

size_t OpenAIWebsocket::base64Decode(const char* input, uint8_t* output, size_t max_output) {
    size_t written = 0;
    if (!input || !output || max_output == 0) {
        return 0;
    }
    if (mbedtls_base64_decode(output, max_output, &written,
                              reinterpret_cast<const uint8_t*>(input),
                              strlen(input)) != 0) {
        return 0;
    }
    return written;
}
bool OpenAIWebsocket::startWorker() {
    if (_worker_running) {
        return true;
    }

    ESP_LOGI(TAG, "OpenAI worker starting (free heap=%u bytes)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

    _worker_running = true;
    bool created = TaskManager::instance().createTask(
        "openai_ws",
        "OpenAIWebsocket",
        workerTask,
        this,
        4,                      // Priority
        1,                      // Core 1
        16384,                  // 16KB stack
        CleanupPattern::GRACEFUL_THEN_FORCE,
        "OpenAI WebSocket communication",
        100
    );
    if (!created) {
        _worker_running = false;
        ESP_LOGE(TAG, "Failed to start OpenAI worker task");
        return false;
    }

    ESP_LOGI(TAG, "OpenAI worker started (free heap=%u bytes)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));

    return true;
}

void OpenAIWebsocket::stopWorker() {
    _worker_running = false;
    TaskManager::instance().stopTask("openai_ws");
}
