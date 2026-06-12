/**
 * GeminiApiClient.cpp
 *
 * Implementation for GeminiApiClient (Gemini Live BidiGenerateContent).
 */

#include "GeminiApiClient.h"
#include "WebsocketClient.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "GeminiApiClient";

GeminiApiClient::GeminiApiClient(WebSocketClient* ws)
    : _ws(ws) {
}

bool GeminiApiClient::sendJson(const JsonDocument& doc) {
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

bool GeminiApiClient::sendText(const String& message) {
    if (!_ws) {
        ESP_LOGW(TAG, "Cannot send message: WebSocket not initialized");
        return false;
    }
    if (!_ws->isConnected() || !_ws->isHealthy()) {
        ESP_LOGW(TAG, "WebSocket not healthy, skipping sendText");
        return false;
    }

    bool sent = _ws->sendTXT(message);
    if (!sent) {
        ESP_LOGE(TAG, "❌ TX failed (len=%u heap=%u psram=%u rssi=%d)",
                 static_cast<unsigned>(message.length()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                 WiFi.RSSI());
    }
    return sent;
}

bool GeminiApiClient::sendSetup(const JsonDocument& setupConfig) {
    return sendJson(setupConfig);
}

void GeminiApiClient::buildSetup(
    JsonDocument& doc,
    const String& model,
    const String& voice,
    const String& instructions,
    int input_sample_rate,
    int vad_silence_duration_ms,
    int vad_prefix_padding_ms
) {
    (void)input_sample_rate;  // documented via realtimeInput mime; not part of setup
    doc.clear();

    JsonObject setup = doc["setup"].to<JsonObject>();

    // Model id must be fully qualified with the "models/" prefix.
    String qualified_model = model;
    if (!qualified_model.startsWith("models/")) {
        qualified_model = String("models/") + qualified_model;
    }
    setup["model"] = qualified_model;

    JsonObject generation = setup["generationConfig"].to<JsonObject>();
    JsonArray modalities = generation["responseModalities"].to<JsonArray>();
    modalities.add("AUDIO");

    JsonObject speech = generation["speechConfig"].to<JsonObject>();
    JsonObject voice_config = speech["voiceConfig"].to<JsonObject>();
    JsonObject prebuilt = voice_config["prebuiltVoiceConfig"].to<JsonObject>();
    prebuilt["voiceName"] = voice;

    // System instruction (persona / behaviour).
    JsonObject system_instruction = setup["systemInstruction"].to<JsonObject>();
    JsonArray sys_parts = system_instruction["parts"].to<JsonArray>();
    JsonObject sys_part = sys_parts.add<JsonObject>();
    sys_part["text"] = instructions;

    // Server-side automatic Voice Activity Detection (turn taking).
    JsonObject realtime_cfg = setup["realtimeInputConfig"].to<JsonObject>();
    JsonObject vad = realtime_cfg["automaticActivityDetection"].to<JsonObject>();
    vad["silenceDurationMs"] = vad_silence_duration_ms;
    vad["prefixPaddingMs"] = vad_prefix_padding_ms;

    // Ask the server to emit transcripts (handy for logging / future captions).
    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();

    // Tools are appended by the caller into:
    //   setup["tools"][0]["functionDeclarations"]
}

bool GeminiApiClient::sendRealtimeAudio(const String& base64_audio, int sample_rate) {
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

    // Optimized string concatenation for large base64 payloads (avoids a full
    // JsonDocument allocation per 10 ms audio frame).
    //   {"realtimeInput":{"audio":{"mimeType":"audio/pcm;rate=16000","data":"..."}}}
    String message =
        String("{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=") +
        String(sample_rate) + "\",\"data\":\"" + base64_audio + "\"}}}";
    return sendText(message);
}

bool GeminiApiClient::sendAudioStreamEnd() {
    JsonDocument doc;
    JsonObject realtime = doc["realtimeInput"].to<JsonObject>();
    realtime["audioStreamEnd"] = true;
    return sendJson(doc);
}

bool GeminiApiClient::sendClientText(const String& text) {
    JsonDocument doc;
    JsonObject client_content = doc["clientContent"].to<JsonObject>();

    JsonArray turns = client_content["turns"].to<JsonArray>();
    JsonObject turn = turns.add<JsonObject>();
    turn["role"] = "user";
    JsonArray parts = turn["parts"].to<JsonArray>();
    JsonObject part = parts.add<JsonObject>();
    part["text"] = text;

    client_content["turnComplete"] = true;

    return sendJson(doc);
}

bool GeminiApiClient::sendToolResponse(const String& callId, const String& name, const String& resultJson) {
    JsonDocument doc;
    JsonObject tool_response = doc["toolResponse"].to<JsonObject>();
    JsonArray function_responses = tool_response["functionResponses"].to<JsonArray>();
    JsonObject fr = function_responses.add<JsonObject>();
    if (callId.length() > 0) {
        fr["id"] = callId;
    }
    fr["name"] = name;

    // The "response" field must be a JSON object. The MCP tool result may be a
    // JSON object, or a bare scalar/string; normalize to an object either way.
    JsonDocument parsed;
    DeserializationError err = deserializeJson(parsed, resultJson);
    if (!err && parsed.is<JsonObject>()) {
        fr["response"] = parsed.as<JsonObject>();
    } else {
        JsonObject response = fr["response"].to<JsonObject>();
        if (!err) {
            response["result"] = parsed.as<JsonVariant>();
        } else {
            response["result"] = resultJson;
        }
    }

    return sendJson(doc);
}
