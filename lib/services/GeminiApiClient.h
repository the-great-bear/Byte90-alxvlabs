/**
 * GeminiApiClient.h
 *
 * Declarations for GeminiApiClient.
 *
 * Mirrors OpenAIApiClient but speaks the Google Gemini Live API
 * (BidiGenerateContent over WebSocket). Encapsulates all Gemini Live
 * message construction and transmission.
 *
 * Protocol reference: Gemini Live API (BidiGenerateContent).
 * Client -> server messages used here:
 *   - setup            (BidiGenerateContentSetup)        : one-time session config
 *   - realtimeInput    (BidiGenerateContentRealtimeInput): streamed PCM16 audio
 *   - clientContent    (BidiGenerateContentClientContent): a text turn (greeting)
 *   - toolResponse     (BidiGenerateContentToolResponse) : MCP tool results
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Forward declarations
class WebSocketClient;

/**
 * @brief Gemini Live API client - handles all API message construction and sending
 */
class GeminiApiClient {
public:
    /**
     * @brief Construct Gemini API client
     *
     * @param ws WebSocket client for sending messages
     */
    GeminiApiClient(WebSocketClient* ws);

    /**
     * @brief Send a fully-formed setup document (BidiGenerateContentSetup)
     *
     * @param setupConfig Setup JSON document ({"setup": {...}})
     * @return true if sent successfully
     */
    bool sendSetup(const JsonDocument& setupConfig);

    /**
     * @brief Build the setup (session config) JSON document
     *
     * Populates the provided document with a {"setup": {...}} payload:
     * model, response modality (AUDIO), voice, system instruction and the
     * automatic VAD config. Tools are appended by the caller into
     * doc["setup"]["tools"][0]["functionDeclarations"].
     *
     * @param doc Output JSON document to populate
     * @param model Fully-qualified model id WITHOUT the "models/" prefix
     *              (e.g. "gemini-2.5-flash-native-audio-preview-12-2025")
     * @param voice Prebuilt voice name (e.g. "Puck", "Charon", "Kore")
     * @param instructions System instructions for the model
     * @param input_sample_rate Sample rate of mic PCM we will stream (Hz)
     * @param vad_silence_duration_ms Silence before end-of-turn (server VAD)
     * @param vad_prefix_padding_ms Audio retained before speech start (server VAD)
     * @param start_sensitivity Gemini start-of-speech sensitivity enum, or ""/nullptr to omit
     * @param end_sensitivity Gemini end-of-speech sensitivity enum, or ""/nullptr to omit
     */
    void buildSetup(
        JsonDocument& doc,
        const String& model,
        const String& voice,
        const String& instructions,
        int input_sample_rate = 16000,
        int vad_silence_duration_ms = 500,
        int vad_prefix_padding_ms = 600,
        const char* start_sensitivity = nullptr,
        const char* end_sensitivity = nullptr
    );

    /**
     * @brief Send streamed PCM16 audio (realtimeInput.audio)
     *
     * @param base64_audio Base64-encoded PCM16 little-endian mono audio
     * @param sample_rate Sample rate of the audio in Hz (mime hint)
     * @return true if sent successfully
     */
    bool sendRealtimeAudio(const String& base64_audio, int sample_rate = 16000);

    /**
     * @brief Signal end of the realtime audio stream (realtimeInput.audioStreamEnd)
     *
     * Useful when manually closing a turn. With automatic VAD this is optional.
     *
     * @return true if sent successfully
     */
    bool sendAudioStreamEnd();

    /**
     * @brief Send a text turn (clientContent) and close the turn
     *
     * Triggers the model to generate a response. Used for the startup greeting.
     *
     * @param text Text content to send as a user turn
     * @return true if sent successfully
     */
    bool sendClientText(const String& text);

    /**
     * @brief Send a tool/function response (toolResponse.functionResponses)
     *
     * @param callId Function call id provided by the server (may be empty)
     * @param name Function name that was called
     * @param resultJson Tool result as a JSON string (object or scalar)
     * @return true if sent successfully
     */
    bool sendToolResponse(const String& callId, const String& name, const String& resultJson);

private:
    WebSocketClient* _ws;

    /**
     * @brief Send JSON document via WebSocket
     */
    bool sendJson(const JsonDocument& doc);

    /**
     * @brief Send raw text message via WebSocket
     */
    bool sendText(const String& message);
};
