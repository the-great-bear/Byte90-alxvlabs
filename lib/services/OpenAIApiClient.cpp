/**
 * OpenAIApiClient.cpp
 *
 * Implementation for OpenAIApiClient.
 */

#include "OpenAIApiClient.h"
#include "WebsocketClient.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "OpenAIApiClient";

static String extractMessageType(const String& message) {
    const char* prefix = "{\"type\":\"";
    if (!message.startsWith(prefix)) {
        return String("");
    }
    int start = strlen(prefix);
    int end = message.indexOf('"', start);
    if (end <= start) {
        return String("");
    }
    return message.substring(start, end);
}

OpenAIApiClient::OpenAIApiClient(WebSocketClient* ws)
    : _ws(ws) {
}

bool OpenAIApiClient::sendJson(const JsonDocument& doc) {
    if (!_ws) {
        ESP_LOGW(TAG, "Cannot send message: WebSocket not initialized");
        return false;
    }
    if (!_ws->isConnected() || !_ws->isHealthy()) {
        ESP_LOGW(TAG, "WebSocket not healthy, skipping sendJson");
        return false;
    }

    String message;
    serializeJson(doc, message);

    bool sent = _ws->sendTXT(message);
    if (sent) {
        ESP_LOGD(TAG, "📤 TX: %s", message.c_str());
    }
    return sent;
}

bool OpenAIApiClient::sendText(const String& message) {
    if (!_ws) {
        ESP_LOGW(TAG, "Cannot send message: WebSocket not initialized");
        return false;
    }
    if (!_ws->isConnected() || !_ws->isHealthy()) {
        ESP_LOGW(TAG, "WebSocket not healthy, skipping sendText");
        return false;
    }

    bool sent = _ws->sendTXT(message);
    if (sent) {
        ESP_LOGD(TAG, "📤 TX: %s", message.c_str());
    } else {
        String type = extractMessageType(message);
        ESP_LOGE(TAG, "❌ TX failed (type=%s len=%u heap=%u psram=%u rssi=%d)",
                 type.length() > 0 ? type.c_str() : "unknown",
                 static_cast<unsigned>(message.length()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                 WiFi.RSSI());
    }
    return sent;
}

bool OpenAIApiClient::sendSessionUpdate(const JsonDocument& sessionConfig) {
    return sendJson(sessionConfig);
}

void OpenAIApiClient::buildSessionUpdate(
    JsonDocument& doc,
    const String& model,
    const String& voice,
    const String& instructions,
    float vad_threshold,
    int vad_prefix_padding_ms,
    int vad_silence_duration_ms
) {
    doc.clear();
    doc["type"] = "session.update";

    JsonObject session = doc["session"].to<JsonObject>();
    session["type"] = "realtime";
    session["model"] = model;

    JsonArray output_modalities = session["output_modalities"].to<JsonArray>();
    output_modalities.add("audio");

    JsonObject audio = session["audio"].to<JsonObject>();

    // Input audio configuration
    JsonObject input = audio["input"].to<JsonObject>();
    JsonObject input_format = input["format"].to<JsonObject>();
    input_format["type"] = "audio/pcm";
    input_format["rate"] = 24000;

    // Noise reduction configuration
    JsonObject noise_reduction = input["noise_reduction"].to<JsonObject>();
    noise_reduction["type"] = "far_field";

    // Voice Activity Detection (VAD) configuration
    JsonObject turn_detection = input["turn_detection"].to<JsonObject>();
    turn_detection["type"] = "server_vad";
    turn_detection["idle_timeout_ms"] = 6000;
    turn_detection["threshold"] = vad_threshold;
    turn_detection["prefix_padding_ms"] = vad_prefix_padding_ms;
    turn_detection["silence_duration_ms"] = vad_silence_duration_ms;
    turn_detection["create_response"] = true;
    turn_detection["interrupt_response"] = true;

    // Transcription configuration
    // JsonObject transcription = input["transcription"].to<JsonObject>();
    // transcription["model"] = "whisper-1";

    // Output audio configuration
    JsonObject output = audio["output"].to<JsonObject>();
    JsonObject output_format = output["format"].to<JsonObject>();
    output_format["type"] = "audio/pcm";
    output_format["rate"] = 24000;
    output["voice"] = voice;

    session["instructions"] = instructions;
    session["tool_choice"] = "none";
}

bool OpenAIApiClient::sendSessionUpdate(
    const String& model,
    const String& voice,
    const String& instructions,
    float vad_threshold,
    int vad_prefix_padding_ms,
    int vad_silence_duration_ms
) {
    JsonDocument doc;
    buildSessionUpdate(
        doc,
        model,
        voice,
        instructions,
        vad_threshold,
        vad_prefix_padding_ms,
        vad_silence_duration_ms
    );

    return sendJson(doc);
}

bool OpenAIApiClient::sendResponseCreate() {
    JsonDocument doc;
    doc["type"] = "response.create";

    return sendJson(doc);
}

bool OpenAIApiClient::sendInputAudioClear() {
    JsonDocument doc;
    doc["type"] = "input_audio_buffer.clear";

    return sendJson(doc);
}

bool OpenAIApiClient::sendResponseCancel(const String& response_id) {
    JsonDocument doc;
    doc["type"] = "response.cancel";

    if (response_id.length() > 0) {
        doc["response_id"] = response_id;
    }

    return sendJson(doc);
}

bool OpenAIApiClient::sendInputAudioAppend(const String& base64_audio) {
    static uint32_t last_log_ms = 0;
    uint32_t now = millis();
    if (now - last_log_ms > 2000) {
        ESP_LOGI(TAG, "Audio append len=%u heap=%u psram=%u rssi=%d",
                 static_cast<unsigned>(base64_audio.length()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                 WiFi.RSSI());
        last_log_ms = now;
    }
    // Use optimized string concatenation for large base64 payloads
    String message = String("{\"type\":\"input_audio_buffer.append\",\"audio\":\"") +
                     base64_audio + "\"}";
    return sendText(message);
}

bool OpenAIApiClient::sendConversationItemCreate(const String& text) {
    JsonDocument doc;
    doc["type"] = "conversation.item.create";

    JsonObject item = doc["item"].to<JsonObject>();
    item["type"] = "message";
    item["role"] = "user";

    JsonArray content = item["content"].to<JsonArray>();
    JsonObject text_content = content.add<JsonObject>();
    text_content["type"] = "input_text";
    text_content["text"] = text;

    return sendJson(doc);
}

bool OpenAIApiClient::sendFunctionCallOutput(const String& callId, const String& result) {
    JsonDocument doc;
    doc["type"] = "conversation.item.create";

    JsonObject item = doc["item"].to<JsonObject>();
    item["type"] = "function_call_output";
    item["call_id"] = callId;
    item["output"] = result;

    return sendJson(doc);
}

bool OpenAIApiClient::sendResponseCreateWithInstructions(const String& instructions) {
    JsonDocument doc;
    doc["type"] = "response.create";

    JsonObject response = doc["response"].to<JsonObject>();

    JsonArray modalities = response["modalities"].to<JsonArray>();
    modalities.add("text");
    modalities.add("audio");

    response["instructions"] = instructions;

    return sendJson(doc);
}
