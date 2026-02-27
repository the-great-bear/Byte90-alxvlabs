/**
 * OpenAIWebsocket.h
 *
 * Declarations for OpenAIWebsocket.
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "McpServer.h"
#include "OpenAiAdapter.h"
#include "WebsocketClient.h"

// Forward declarations
class OpenAIApiClient;

/**
 * OpenAIWebsocket - OpenAI realtime PCM streaming client
 *
 * Features:
 * - WebSocket connection to OpenAI realtime API
 * - Base64 PCM16 audio upload
 * - PCM ring buffer for playback
 */
class OpenAIWebsocket {
public:
    OpenAIWebsocket();
    ~OpenAIWebsocket();

    /**
     * @brief Allocate buffers and initialize client
     *
     * @return true on success, false otherwise
     */
    bool begin();

    /**
     * @brief Begin WebSocket connection
     */
    void connect();

    /**
     * @brief Disconnect WebSocket connection
     */
    void disconnect();

    /**
     * @brief Poll WebSocket and process incoming data
     */
    void poll();

    /**
     * @brief Check if WebSocket is connected
     *
     * @return true if connected, false otherwise
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Check if OpenAI session is ready for audio
     *
     * @return true if session updated, false otherwise
     */
    bool isSessionReady() const { return _session_ready; }
    bool isSpeaking() const { return _speaking; }
    uint32_t lastUserSpeechMs() const { return _last_user_speech_ms; }
    void setMinSpeechMs(int min_ms) { _min_speech_ms = min_ms; }

    /**
     * @brief Configure Voice Activity Detection (VAD) parameters
     *
     * These settings are sent to OpenAI server in session.update.
     * Call before connect() to take effect on next session.
     *
     * @param threshold VAD sensitivity (0.0-1.0, default 0.3)
     * @param prefix_padding_ms Audio before speech start (default 600ms)
     * @param silence_duration_ms Silence before turn end (default 500ms)
     */
    void setVadThreshold(float threshold) { _vad_threshold = threshold; }
    void setVadPrefixPadding(int ms) { _vad_prefix_padding_ms = ms; }
    void setVadSilenceDuration(int ms) { _vad_silence_duration_ms = ms; }

    void onSpeechState(std::function<void(bool)> callback);
    void onBeforeResponseCreate(std::function<void()> callback);
    void onResponseDone(std::function<void()> callback);
    void onOutputAudioDone(std::function<void()> callback);
    void onToolCallStart(std::function<void()> callback);
    void onToolCallEnd(std::function<void()> callback);

    /**
     * @brief Set the OpenAI API key used for Authorization
     *
     * @param api_key API key string
     */
    void setApiKey(const String& api_key) { _api_key = api_key; }
    bool hasApiKey() const { return !_api_key.isEmpty(); }

    /**
     * @brief Send PCM16 audio samples to OpenAI
     *
     * @param samples PCM16 samples
     * @param sample_count Number of samples
     * @return true if sent, false otherwise
     */
    bool sendPcm(const int16_t* samples, size_t sample_count);
    bool enqueuePcm(const int16_t* samples, size_t sample_count);

    /**
     * @brief Pop PCM16 samples for playback
     *
     * @param out Output buffer for samples
     * @param max_samples Maximum samples to read
     * @return Samples read
     */
    size_t popPcm(int16_t* out, size_t max_samples);

    /**
     * @brief Clear server-side input audio buffer
     *
     * Sends input_audio_buffer.clear event to discard any queued audio
     * on the OpenAI server. Use this to prevent echo from triggering VAD.
     */
    void clearInputAudioBuffer() { sendInputAudioClear(); }

    /**
     * @brief Clear local TX ring buffer
     */
    void clearTxRingBuffer() { clearTxRing(); }

    /**
     * @brief Cancel any in-progress response on the server
     *
     * Sends response.cancel event to abort any ongoing response processing.
     * Safe to call even if no response is in progress.
     */
    void cancelResponse() {
        if (_current_response_id.length() > 0) {
            sendResponseCancel(_current_response_id);
        } else {
            sendResponseCancel(_last_response_id);
        }
    }

    /**
     * @brief Clear local playback ring buffer
     */
    void clearPlaybackBuffer() {
        portENTER_CRITICAL(&_ring_mux);
        _head = _tail;
        portEXIT_CRITICAL(&_ring_mux);
    }

    /**
     * @brief Send a text message and request a response
     *
     * @param text User text to send
     * @return true if sent, false otherwise
     */
    bool sendTextResponse(const String& text);

    /**
     * @brief Get the last response ID from the server
     *
     * @return Last response ID string, or empty if none
     */
    String getLastResponseId() const { return _last_response_id; }
    String getCurrentResponseId() const { return _current_response_id; }

    /**
     * @brief Check if playback buffer is empty
     *
     * @return true if no audio data in playback buffer, false otherwise
     */
    bool isPlaybackBufferEmpty() const {
        portENTER_CRITICAL(&_ring_mux);
        bool empty = (_head == _tail);
        portEXIT_CRITICAL(&_ring_mux);
        return empty;
    }

    /**
     * @brief Register connected callback
     *
     * @param callback Function to call on connect
     */
    void onConnected(std::function<void()> callback);

    /**
     * @brief Register disconnected callback
     *
     * @param callback Function to call on disconnect
     */
    void onDisconnected(std::function<void()> callback);
    void onNetworkError(std::function<void(const String&)> callback);
    bool startWorker();
    void stopWorker();
    bool isWorkerRunning() const { return _worker_running; }
    void setMcpServer(McpServer* server);

private:
    void handleEvent(WSTYPE_t type, uint8_t* payload, size_t length);
    void handleText(const char* data, size_t length);
    void sendSessionUpdate();
    void sendResponseCreate();
    void sendInputAudioClear();
    void sendResponseCancel(const String& response_id);
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
    OpenAIApiClient* _api;
    bool _connected;
    bool _session_ready;
    bool _auto_reconnect;
    bool _speaking;
    bool _response_in_progress;
    bool _sent_startup_text;
    int _min_speech_ms;
    int _last_speech_start_ms;
    uint32_t _last_user_speech_ms;
    uint32_t _last_input_clear_ms;
    String _last_response_id;
    String _current_response_id;

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
    OpenAiAdapter* _ai_adapter;

    // VAD configuration (tunable at runtime)
    float _vad_threshold;
    int _vad_prefix_padding_ms;
    int _vad_silence_duration_ms;
};
