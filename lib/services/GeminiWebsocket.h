/**
 * GeminiWebsocket.h
 *
 * Declarations for GeminiWebsocket.
 *
 * Real-time PCM streaming client for the Google Gemini Live API
 * (BidiGenerateContent over WebSocket). This is a sibling of OpenAIWebsocket
 * and exposes an IDENTICAL public interface so the rest of the firmware
 * (ProtocolManager, ApplicationAudio, AudioService) can talk to either
 * provider through the RealtimeAiClient alias selected at build time.
 *
 * Differences from OpenAIWebsocket are internal only:
 *   - Auth: API key passed in the WebSocket URL query string (?key=...).
 *   - Wire protocol: Gemini "setup" / "realtimeInput" / "toolResponse" frames
 *     and "serverContent" / "toolCall" server messages.
 *   - Audio: input streamed as 16 kHz PCM16 (native mic rate, no resample),
 *     output received as 24 kHz PCM16.
 *   - Turn taking: server-side automatic VAD; barge-in via serverContent.interrupted.
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "McpServer.h"
#include "GeminiAdapter.h"
#include "WebsocketClient.h"

// Forward declarations
class GeminiApiClient;

/**
 * GeminiWebsocket - Gemini Live realtime PCM streaming client
 */
class GeminiWebsocket {
public:
    GeminiWebsocket();
    ~GeminiWebsocket();

    bool begin();
    void connect();
    void disconnect();
    void poll();

    bool isConnected() const { return _connected; }
    bool isSessionReady() const { return _session_ready; }
    bool isSpeaking() const { return _speaking; }
    uint32_t lastUserSpeechMs() const { return _last_user_speech_ms; }
    void setMinSpeechMs(int min_ms) { _min_speech_ms = min_ms; }

    // VAD configuration. Gemini uses server-side automatic activity detection;
    // threshold is accepted for interface compatibility but only the silence
    // and prefix-padding durations are forwarded to the server.
    void setVadThreshold(float threshold) { _vad_threshold = threshold; }
    void setVadPrefixPadding(int ms) { _vad_prefix_padding_ms = ms; }
    void setVadSilenceDuration(int ms) { _vad_silence_duration_ms = ms; }

    void onSpeechState(std::function<void(bool)> callback);
    void onBeforeResponseCreate(std::function<void()> callback);
    void onResponseDone(std::function<void()> callback);
    void onOutputAudioDone(std::function<void()> callback);
    void onToolCallStart(std::function<void()> callback);
    void onToolCallEnd(std::function<void()> callback);

    void setApiKey(const String& api_key) { _api_key = api_key; }
    bool hasApiKey() const { return !_api_key.isEmpty(); }

    bool sendPcm(const int16_t* samples, size_t sample_count);
    bool enqueuePcm(const int16_t* samples, size_t sample_count);
    size_t popPcm(int16_t* out, size_t max_samples);

    // With automatic VAD there is no server-side input buffer to clear; this
    // clears the local TX ring so queued mic audio is not streamed (used to
    // suppress echo while the model is speaking).
    void clearInputAudioBuffer() { clearTxRing(); }
    void clearTxRingBuffer() { clearTxRing(); }

    // Gemini cancels the current response automatically when the user barges in
    // (serverContent.interrupted). cancelResponse() stops local playback and
    // marks the turn finished; there is no explicit cancel frame to send.
    void cancelResponse() {
        clearPlaybackBuffer();
        clearTxRing();
        _current_response_id = "";
        updateSpeaking(false);
    }

    void clearPlaybackBuffer() {
        portENTER_CRITICAL(&_ring_mux);
        _head = _tail;
        portEXIT_CRITICAL(&_ring_mux);
    }

    bool sendTextResponse(const String& text);

    String getLastResponseId() const { return _last_response_id; }
    String getCurrentResponseId() const { return _current_response_id; }

    bool isPlaybackBufferEmpty() const {
        portENTER_CRITICAL(&_ring_mux);
        bool empty = (_head == _tail);
        portEXIT_CRITICAL(&_ring_mux);
        return empty;
    }

    void onConnected(std::function<void()> callback);
    void onDisconnected(std::function<void()> callback);
    void onNetworkError(std::function<void(const String&)> callback);
    bool startWorker();
    void stopWorker();
    bool isWorkerRunning() const { return _worker_running; }
    void setMcpServer(McpServer* server);

private:
    void handleEvent(WSTYPE_t type, uint8_t* payload, size_t length);
    void handleText(const char* data, size_t length);
    void handleServerContent(JsonObject server_content);
    void handleToolCall(JsonObject tool_call);
    void sendSetup();
    void clearTxRing();
    void drainTxQueue();
    void updateSpeaking(bool speaking);
    bool pushPcmBytes(const uint8_t* data, size_t length);
    size_t popPcmBytes(uint8_t* out, size_t max_bytes);
    String base64Encode(const uint8_t* data, size_t length);
    size_t base64Decode(const char* input, uint8_t* output, size_t max_output);
    static void workerTask(void* parameter);
    void workerLoop();
    static void toolWorkerTask(void* parameter);
    void toolWorkerLoop();
    void processToolResults();

    WebSocketClient _ws;
    GeminiApiClient* _api;
    bool _connected;
    bool _session_ready;
    bool _auto_reconnect;
    bool _speaking;
    bool _response_in_progress;
    bool _sent_startup_text;
    bool _turn_audio_started;
    int _min_speech_ms;
    uint32_t _last_user_speech_ms;
    String _last_response_id;
    String _current_response_id;
    uint32_t _response_counter;

    std::function<void()> _on_connected;
    std::function<void()> _on_disconnected;
    std::function<void(const String&)> _on_network_error;
    std::function<void(bool)> _on_speech_state;
    std::function<void()> _on_before_response_create;
    std::function<void()> _on_response_done;
    std::function<void()> _on_output_audio_done;
    std::function<void()> _on_tool_call_start;
    std::function<void()> _on_tool_call_end;

    String _api_key;

    uint8_t* _ring;
    size_t _ring_size;
    volatile size_t _head;
    volatile size_t _tail;
    mutable portMUX_TYPE _ring_mux;

    uint8_t* _tx_ring;
    size_t _tx_ring_size;
    volatile size_t _tx_head;
    volatile size_t _tx_tail;
    portMUX_TYPE _tx_mux;
    volatile bool _worker_running;
    QueueHandle_t _tool_call_queue;
    QueueHandle_t _tool_result_queue;
    volatile bool _tool_worker_running;
    McpServer* _mcp_server;
    GeminiAdapter* _ai_adapter;

    // VAD configuration (tunable at runtime)
    float _vad_threshold;
    int _vad_prefix_padding_ms;
    int _vad_silence_duration_ms;
};
