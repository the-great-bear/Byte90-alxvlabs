/**
 * GeminiWebsocket.cpp
 *
 * Implementation for GeminiWebsocket (Google Gemini Live BidiGenerateContent).
 *
 * Audio/MCP plumbing (SPIRAM ring buffers, worker tasks, tool queues, base64)
 * mirrors OpenAIWebsocket; only the wire protocol differs.
 */

#include "GeminiWebsocket.h"
#include "GeminiApiClient.h"

#include "DeviceConfig.h"
#include "TaskManager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "GeminiWebsocket";

namespace {
constexpr size_t GEMINI_PCM_RING_BYTES = 2048 * 1024;  // 2MB = ~43.7 s at 24kHz output
constexpr size_t GEMINI_PCM_BYTES_PER_SAMPLE = 2;
constexpr size_t GEMINI_PCM_CHUNK_SAMPLES = 320;  // 20 ms at 16 kHz input
constexpr size_t GEMINI_TX_RING_BYTES = 32 * 1024;
constexpr size_t GEMINI_TOOL_QUEUE_DEPTH = 4;
} // namespace

struct GeminiToolCallItem {
    String call_id;
    String name;
    String args;
};

struct GeminiToolResultItem {
    String call_id;
    String name;
    String result;
};

GeminiWebsocket::GeminiWebsocket()
    : _api(nullptr)
    , _connected(false)
    , _session_ready(false)
    , _auto_reconnect(false)
    , _speaking(false)
    , _response_in_progress(false)
    , _sent_startup_text(false)
    , _turn_audio_started(false)
    , _min_speech_ms(500)
    , _last_user_speech_ms(0)
    , _response_counter(0)
    , _api_key("")
    , _ring(nullptr)
    , _ring_size(GEMINI_PCM_RING_BYTES)
    , _head(0)
    , _tail(0)
    , _ring_mux(portMUX_INITIALIZER_UNLOCKED)
    , _tx_ring(nullptr)
    , _tx_ring_size(GEMINI_TX_RING_BYTES)
    , _tx_head(0)
    , _tx_tail(0)
    , _tx_mux(portMUX_INITIALIZER_UNLOCKED)
    , _worker_running(false)
    , _tool_call_queue(nullptr)
    , _tool_result_queue(nullptr)
    , _tool_worker_running(false)
    , _mcp_server(nullptr)
    , _ai_adapter(nullptr)
    , _vad_threshold(0.3f)
    , _vad_prefix_padding_ms(600)
    , _vad_silence_duration_ms(500)
{
    _api = new GeminiApiClient(&_ws);
}

GeminiWebsocket::~GeminiWebsocket() {
    stopWorker();
    _tool_worker_running = false;
    TaskManager::instance().stopTask("gemini_tool");
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

bool GeminiWebsocket::begin() {
    if (_ring != nullptr) {
        return true;
    }

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

    ESP_LOGI(TAG, "Allocated %u KB for audio buffers (RX: %u KB, TX: %u KB)",
             total_needed / 1024, _ring_size / 1024, _tx_ring_size / 1024);

    _ws.onEvent([this](WSTYPE_t type, uint8_t* payload, size_t length) {
        handleEvent(type, payload, length);
    });

    if (!_tool_call_queue) {
        _tool_call_queue = xQueueCreate(GEMINI_TOOL_QUEUE_DEPTH, sizeof(GeminiToolCallItem*));
    }
    if (!_tool_result_queue) {
        _tool_result_queue = xQueueCreate(GEMINI_TOOL_QUEUE_DEPTH, sizeof(GeminiToolResultItem*));
    }
    if (_tool_call_queue && _tool_result_queue && !_tool_worker_running) {
        _tool_worker_running = true;
        bool created = TaskManager::instance().createTask(
            "gemini_tool",
            "GeminiWebsocket",
            toolWorkerTask,
            this,
            3,                      // Priority
            0,                      // Core 0
            8192,                   // 8KB stack
            CleanupPattern::FORCE_DELETE,
            "Gemini MCP tool call processing"
        );
        if (!created) {
            _tool_worker_running = false;
            ESP_LOGE(TAG, "Failed to start Gemini tool worker");
        }
    }

    return startWorker();
}

void GeminiWebsocket::connect() {
    startWorker();
    if (_api_key.isEmpty()) {
        ESP_LOGE(TAG, "Gemini API key missing, cannot connect");
        _auto_reconnect = false;
        return;
    }

    _auto_reconnect = true;
    // Gemini Live authenticates with the API key in the URL query string; there
    // is no Authorization header (none is set on this client).
    _ws.setReconnectInterval(1000);
    _ws.setMaxReconnectAttempts(PROTOCOL_MAX_RECONNECT_ATTEMPTS);

    String path = String(GEMINI_LIVE_PATH) + _api_key;
    _ws.beginSSL(GEMINI_LIVE_HOST,
                 GEMINI_LIVE_PORT,
                 path.c_str(),
                 ROOT_CA_CERTIFICATE,
                 "gemini-live");
}

void GeminiWebsocket::disconnect() {
    _auto_reconnect = false;
    stopWorker();
    _ws.disconnect();
    _connected = false;
    _session_ready = false;
    _response_in_progress = false;
    _sent_startup_text = false;
    _turn_audio_started = false;
    _last_user_speech_ms = 0;
    _current_response_id = "";
    updateSpeaking(false);
}

void GeminiWebsocket::poll() {
    if (_worker_running) {
        return;
    }
    if (!_auto_reconnect && !_connected) {
        return;
    }
    _ws.loop();
    drainTxQueue();
}

void GeminiWebsocket::onNetworkError(std::function<void(const String&)> callback) {
    _on_network_error = callback;
}

bool GeminiWebsocket::sendPcm(const int16_t* samples, size_t sample_count) {
    static uint32_t last_failure_log = 0;
    static int failure_count = 0;

    if (!_connected || !_session_ready || !samples || sample_count == 0 || !_api) {
        if (++failure_count % 100 == 0 || millis() - last_failure_log > 2000) {
            ESP_LOGW(TAG, "🔴 sendPcm blocked: connected=%d session_ready=%d count=%u (failed %d times)",
                     _connected, _session_ready, sample_count, failure_count);
            last_failure_log = millis();
            failure_count = 0;
        }
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
    size_t byte_count = sample_count * GEMINI_PCM_BYTES_PER_SAMPLE;
    String b64 = base64Encode(bytes, byte_count);

    bool result = _api->sendRealtimeAudio(b64, AUDIO_SAMPLE_RATE_STT);
    if (!result) {
        if (++failure_count % 100 == 0 || millis() - last_failure_log > 2000) {
            ESP_LOGE(TAG, "🔴 sendRealtimeAudio FAILED (size: %u bytes, failed %d times)", byte_count, failure_count);
            last_failure_log = millis();
        }
        if (_on_network_error) {
            _on_network_error("Gemini sendRealtimeAudio failed");
        }
    } else {
        failure_count = 0;
    }

    return result;
}

bool GeminiWebsocket::enqueuePcm(const int16_t* samples, size_t sample_count) {
    if (!samples || sample_count == 0 || !_tx_ring) {
        return false;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
    size_t byte_count = sample_count * GEMINI_PCM_BYTES_PER_SAMPLE;

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

size_t GeminiWebsocket::popPcm(int16_t* out, size_t max_samples) {
    if (!out || max_samples == 0) {
        return 0;
    }

    size_t max_bytes = max_samples * GEMINI_PCM_BYTES_PER_SAMPLE;
    size_t bytes_read = popPcmBytes(reinterpret_cast<uint8_t*>(out), max_bytes);
    return bytes_read / GEMINI_PCM_BYTES_PER_SAMPLE;
}

void GeminiWebsocket::onConnected(std::function<void()> callback) { _on_connected = callback; }
void GeminiWebsocket::onDisconnected(std::function<void()> callback) { _on_disconnected = callback; }

void GeminiWebsocket::setMcpServer(McpServer* server) {
    _mcp_server = server;
    if (_ai_adapter) {
        delete _ai_adapter;
        _ai_adapter = nullptr;
    }
    if (_mcp_server) {
        _ai_adapter = new GeminiAdapter(_mcp_server);
    }
}

void GeminiWebsocket::onSpeechState(std::function<void(bool)> callback) { _on_speech_state = callback; }
void GeminiWebsocket::onBeforeResponseCreate(std::function<void()> callback) { _on_before_response_create = callback; }
void GeminiWebsocket::onResponseDone(std::function<void()> callback) { _on_response_done = callback; }
void GeminiWebsocket::onOutputAudioDone(std::function<void()> callback) { _on_output_audio_done = callback; }
void GeminiWebsocket::onToolCallStart(std::function<void()> callback) { _on_tool_call_start = callback; }
void GeminiWebsocket::onToolCallEnd(std::function<void()> callback) { _on_tool_call_end = callback; }

void GeminiWebsocket::handleEvent(WSTYPE_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WSTYPE_CONNECTED:
            _connected = true;
            _session_ready = false;
            _sent_startup_text = false;
            // Gemini requires the setup message as the first client frame.
            sendSetup();
            if (_on_connected) {
                _on_connected();
            }
            break;
        case WSTYPE_ERROR:
            if (_on_network_error) {
                if (payload && length > 0) {
                    _on_network_error(String(reinterpret_cast<const char*>(payload), length));
                } else {
                    _on_network_error("gemini_ws_error");
                }
            }
            break;
        case WSTYPE_DISCONNECTED:
            _connected = false;
            _session_ready = false;
            _sent_startup_text = false;
            _turn_audio_started = false;
            if (_on_disconnected) {
                _on_disconnected();
            }
            break;
        case WSTYPE_TEXT:
        case WSTYPE_BIN:
            if (payload && length > 0) {
                handleText(reinterpret_cast<const char*>(payload), length);
            }
            break;
        default:
            break;
    }
}

void GeminiWebsocket::handleText(const char* data, size_t length) {
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
        ESP_LOGW(TAG, "Failed to parse message: %s (len=%u, preview=\"%s\")",
                 error.c_str(), static_cast<unsigned int>(length), preview);
        return;
    }

    if (!doc["setupComplete"].isNull()) {
        ESP_LOGI(TAG, "[Setup Complete]");
        _session_ready = true;
        _last_user_speech_ms = millis();
        if (_mcp_server) {
            _response_counter++;
            _mcp_server->setSessionId(String("gemini-") + String(_response_counter));
        }
        // Kick off the conversation with a greeting turn.
        if (!_sent_startup_text && GEMINI_STARTUP_TEXT[0] != '\0' && _api) {
            _api->sendClientText(GEMINI_STARTUP_TEXT);
            _sent_startup_text = true;
        }
        return;
    }

    if (!doc["serverContent"].isNull()) {
        handleServerContent(doc["serverContent"].as<JsonObject>());
        return;
    }

    if (!doc["toolCall"].isNull()) {
        handleToolCall(doc["toolCall"].as<JsonObject>());
        return;
    }

    if (!doc["toolCallCancellation"].isNull()) {
        ESP_LOGI(TAG, "[Tool Call Cancellation]");
        return;
    }

    if (!doc["goAway"].isNull()) {
        int time_left = doc["goAway"]["timeLeft"] | -1;
        ESP_LOGW(TAG, "[GoAway] server closing soon, timeLeft=%d", time_left);
        return;
    }

    // sessionResumptionUpdate, usageMetadata, etc. are ignored.
}

void GeminiWebsocket::handleServerContent(JsonObject server_content) {
    if (server_content.isNull()) {
        return;
    }

    // User barge-in: the model's current generation was interrupted.
    if (server_content["interrupted"].as<bool>()) {
        ESP_LOGI(TAG, "[Interrupted] user barge-in");
        clearPlaybackBuffer();
        clearTxRing();
        _turn_audio_started = false;
        _response_in_progress = false;
        _current_response_id = "";
        updateSpeaking(false);
        return;
    }

    // Transcripts (optional, logged for diagnostics).
    const char* input_tx = server_content["inputTranscription"]["text"] | "";
    if (input_tx && input_tx[0] != '\0') {
        _last_user_speech_ms = millis();
        ESP_LOGI(TAG, "[User] %s", input_tx);
    }
    const char* output_tx = server_content["outputTranscription"]["text"] | "";
    if (output_tx && output_tx[0] != '\0') {
        ESP_LOGI(TAG, "[Model] %s", output_tx);
    }

    JsonObject model_turn = server_content["modelTurn"].as<JsonObject>();
    if (!model_turn.isNull()) {
        JsonArray parts = model_turn["parts"].as<JsonArray>();
        for (JsonVariant part_v : parts) {
            JsonObject part = part_v.as<JsonObject>();
            if (part.isNull()) {
                continue;
            }
            // Audio output arrives as base64 PCM16 @ 24 kHz in inlineData.
            JsonObject inline_data = part["inlineData"].as<JsonObject>();
            if (!inline_data.isNull()) {
                const char* mime = inline_data["mimeType"] | "";
                const char* b64 = inline_data["data"] | "";
                if (b64 && b64[0] != '\0' && (mime == nullptr || strstr(mime, "audio") != nullptr)) {
                    // First audio of a new turn: announce response start.
                    if (!_turn_audio_started) {
                        _turn_audio_started = true;
                        _response_in_progress = true;
                        _response_counter++;
                        _current_response_id = String("gemini-resp-") + String(_response_counter);
                        _last_user_speech_ms = millis();
                        if (_on_before_response_create) {
                            _on_before_response_create();
                        }
                        updateSpeaking(true);
                    }

                    size_t input_len = strlen(b64);
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
                        size_t decoded = base64Decode(b64, decode_buffer, buffer_size);
                        if (decoded > 0) {
                            pushPcmBytes(decode_buffer, decoded);
                        }
                    }

                    if (decode_buffer != stack_buffer && decode_buffer != nullptr) {
                        heap_caps_free(decode_buffer);
                    }
                }
            }

            const char* text = part["text"] | "";
            if (text && text[0] != '\0') {
                ESP_LOGI(TAG, "[Text] %s", text);
            }
        }
    }

    // generationComplete: the model finished producing output audio for the turn.
    if (server_content["generationComplete"].as<bool>()) {
        ESP_LOGI(TAG, "[Generation Complete]");
        _last_user_speech_ms = millis();
        if (_on_output_audio_done) {
            _on_output_audio_done();
        }
    }

    // turnComplete: the full response turn is done; hand back the conversation.
    if (server_content["turnComplete"].as<bool>()) {
        ESP_LOGI(TAG, "[Turn Complete]");
        _last_response_id = _current_response_id;
        _current_response_id = "";
        _response_in_progress = false;
        _turn_audio_started = false;
        _last_user_speech_ms = millis();
        updateSpeaking(false);
        if (_on_response_done) {
            _on_response_done();
        }
    }
}

void GeminiWebsocket::handleToolCall(JsonObject tool_call) {
    if (tool_call.isNull()) {
        return;
    }
    JsonArray function_calls = tool_call["functionCalls"].as<JsonArray>();
    for (JsonVariant fc_v : function_calls) {
        JsonObject fc = fc_v.as<JsonObject>();
        if (fc.isNull()) {
            continue;
        }
        const char* id = fc["id"] | "";
        const char* name = fc["name"] | "";
        String args;
        if (!fc["args"].isNull()) {
            serializeJson(fc["args"], args);
        } else {
            args = "{}";
        }

        ESP_LOGI(TAG, "[Tool Call] id=%s name=%s args=%s", id, name, args.c_str());

        if (_tool_call_queue) {
            GeminiToolCallItem* item = new GeminiToolCallItem{ String(id), String(name), args };
            if (xQueueSend(_tool_call_queue, &item, 0) != pdTRUE) {
                delete item;
                ESP_LOGW(TAG, "Tool queue full, dropping tool call");
                if (_api) {
                    _api->sendToolResponse(String(id), String(name), "{\"error\":\"tool queue full\"}");
                }
            } else if (_on_tool_call_start) {
                _on_tool_call_start();
            }
        }
    }
}

void GeminiWebsocket::sendSetup() {
    if (!_api) {
        return;
    }

    JsonDocument doc;
    _api->buildSetup(
        doc,
        GEMINI_LIVE_MODEL,
        GEMINI_LIVE_VOICE,
        GEMINI_LIVE_INSTRUCTIONS,
        AUDIO_SAMPLE_RATE_STT,
        _vad_silence_duration_ms,
        _vad_prefix_padding_ms
    );

    // Append MCP tools as Gemini function declarations.
    if (_ai_adapter) {
        JsonObject setup = doc["setup"].as<JsonObject>();
        JsonArray tools = setup["tools"].to<JsonArray>();
        JsonObject tool0 = tools.add<JsonObject>();
        JsonArray decls = tool0["functionDeclarations"].to<JsonArray>();
        _ai_adapter->generateSchema(decls);
        if (decls.size() == 0) {
            setup.remove("tools");
        }
    }

    ESP_LOGI(TAG, "Sending setup (model=%s voice=%s vad_silence=%dms)",
             GEMINI_LIVE_MODEL, GEMINI_LIVE_VOICE, _vad_silence_duration_ms);
    _api->sendSetup(doc);
}

bool GeminiWebsocket::sendTextResponse(const String& text) {
    if (!_api || !_connected || !_session_ready) {
        return false;
    }
    if (text.length() == 0) {
        return false;
    }
    if (!_api->sendClientText(text)) {
        if (_on_network_error) {
            _on_network_error("Gemini sendClientText failed");
        }
        return false;
    }
    return true;
}

void GeminiWebsocket::clearTxRing() {
    if (!_tx_ring) {
        return;
    }
    portENTER_CRITICAL(&_tx_mux);
    _tx_head = _tx_tail;
    portEXIT_CRITICAL(&_tx_mux);
}

void GeminiWebsocket::updateSpeaking(bool speaking) {
    if (_speaking == speaking) {
        return;
    }
    ESP_LOGI(TAG, "[Speaking State] %s -> %s", _speaking ? "true" : "false", speaking ? "true" : "false");
    _speaking = speaking;
    if (speaking) {
        clearTxRing();
    } else {
        clearTxRing();
    }
    if (_on_speech_state) {
        _on_speech_state(speaking);
    }
}

void GeminiWebsocket::workerTask(void* parameter) {
    GeminiWebsocket* client = static_cast<GeminiWebsocket*>(parameter);
    if (client) {
        client->workerLoop();
    }
    TaskManager::instance().markTaskStopped("gemini_ws");
    vTaskDelete(nullptr);
}

void GeminiWebsocket::workerLoop() {
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

void GeminiWebsocket::toolWorkerTask(void* parameter) {
    GeminiWebsocket* client = static_cast<GeminiWebsocket*>(parameter);
    if (client) {
        client->toolWorkerLoop();
    }
    TaskManager::instance().markTaskStopped("gemini_tool");
    vTaskDelete(nullptr);
}

void GeminiWebsocket::toolWorkerLoop() {
    while (_tool_worker_running) {
        GeminiToolCallItem* item = nullptr;
        if (_tool_call_queue &&
            xQueueReceive(_tool_call_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (!item) {
                continue;
            }

            String result = "{\"error\":\"tool adapter not available\"}";
            if (_ai_adapter) {
                result = _ai_adapter->executeTool(item->name, item->args);
            }

            GeminiToolResultItem* out = new GeminiToolResultItem{ item->call_id, item->name, result };
            if (!_tool_result_queue ||
                xQueueSend(_tool_result_queue, &out, 0) != pdTRUE) {
                delete out;
                ESP_LOGW(TAG, "Tool result queue full, dropping result");
            }

            delete item;
        }
    }
}

void GeminiWebsocket::processToolResults() {
    if (!_tool_result_queue || !_api) {
        return;
    }

    GeminiToolResultItem* result = nullptr;
    while (xQueueReceive(_tool_result_queue, &result, 0) == pdTRUE) {
        if (!result) {
            continue;
        }
        ESP_LOGI(TAG, "[Tool Result] %s", result->result.c_str());
        // Sending the tool response automatically resumes model generation.
        _api->sendToolResponse(result->call_id, result->name, result->result);
        if (_on_tool_call_end) {
            _on_tool_call_end();
        }
        delete result;
    }
}

void GeminiWebsocket::drainTxQueue() {
    static uint32_t last_warning = 0;

    if (!_connected || !_session_ready || !_tx_ring) {
        portENTER_CRITICAL(&_tx_mux);
        size_t used = (_tx_head + _tx_ring_size - _tx_tail) % _tx_ring_size;
        portEXIT_CRITICAL(&_tx_mux);
        if (used > _tx_ring_size / 2 && millis() - last_warning > 2000) {
            ESP_LOGW(TAG, "🟡 TX queue blocked: connected=%d session_ready=%d used=%u/%u bytes",
                     _connected, _session_ready, used, _tx_ring_size);
            last_warning = millis();
        }
        return;
    }

    uint8_t buffer[GEMINI_PCM_CHUNK_SAMPLES * GEMINI_PCM_BYTES_PER_SAMPLE];
    size_t max_bytes = sizeof(buffer);

    for (;;) {
        portENTER_CRITICAL(&_tx_mux);
        size_t used = (_tx_head + _tx_ring_size - _tx_tail) % _tx_ring_size;
        if (used == 0) {
            portEXIT_CRITICAL(&_tx_mux);
            break;
        }

        size_t take = min(used, max_bytes);
        size_t first = min(take, _tx_ring_size - _tx_tail);
        memcpy(buffer, _tx_ring + _tx_tail, first);
        memcpy(buffer + first, _tx_ring, take - first);
        _tx_tail = (_tx_tail + take) % _tx_ring_size;
        portEXIT_CRITICAL(&_tx_mux);

        const int16_t* samples = reinterpret_cast<const int16_t*>(buffer);
        size_t sample_count = take / GEMINI_PCM_BYTES_PER_SAMPLE;
        if (!sendPcm(samples, sample_count)) {
            ESP_LOGW(TAG, "🔴 drainTxQueue: sendPcm failed (queue used: %u bytes)", used);
            break;
        }
    }
}

bool GeminiWebsocket::pushPcmBytes(const uint8_t* data, size_t length) {
    if (!data || length == 0 || !_ring) {
        return false;
    }

    portENTER_CRITICAL(&_ring_mux);
    size_t free_bytes = (_tail + _ring_size - _head - 1) % _ring_size;
    size_t used_bytes = (_head + _ring_size - _tail) % _ring_size;
    if (length > free_bytes) {
        portEXIT_CRITICAL(&_ring_mux);
        static uint32_t last_overflow_log = 0;
        uint32_t now = millis();
        if (now - last_overflow_log > 1000) {
            ESP_LOGW(TAG, "PCM ring buffer full! Dropping %u bytes (used: %u/%u)",
                     length, used_bytes, _ring_size);
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

size_t GeminiWebsocket::popPcmBytes(uint8_t* out, size_t max_bytes) {
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

String GeminiWebsocket::base64Encode(const uint8_t* data, size_t length) {
    size_t out_len = ((length + 2) / 3) * 4 + 1;
    char* buffer = static_cast<char*>(alloca(out_len));
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<uint8_t*>(buffer), out_len, &written, data, length) != 0) {
        return String();
    }
    buffer[written] = '\0';
    return String(buffer);
}

size_t GeminiWebsocket::base64Decode(const char* input, uint8_t* output, size_t max_output) {
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

bool GeminiWebsocket::startWorker() {
    if (_worker_running) {
        return true;
    }

    _worker_running = true;
    bool created = TaskManager::instance().createTask(
        "gemini_ws",
        "GeminiWebsocket",
        workerTask,
        this,
        4,                      // Priority
        1,                      // Core 1
        16384,                  // 16KB stack
        CleanupPattern::GRACEFUL_THEN_FORCE,
        "Gemini WebSocket communication",
        100
    );
    if (!created) {
        _worker_running = false;
        ESP_LOGE(TAG, "Failed to start Gemini worker task");
        return false;
    }

    return true;
}

void GeminiWebsocket::stopWorker() {
    _worker_running = false;
    TaskManager::instance().stopTask("gemini_ws");
}
