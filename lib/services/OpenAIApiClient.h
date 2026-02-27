/**
 * OpenAIApiClient.h
 *
 * Declarations for OpenAIApiClient.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Forward declarations
class WebSocketClient;

/**
 * @brief OpenAI Realtime API client - handles all API message construction and sending
 *
 * Encapsulates OpenAI Realtime API message construction and transmission.
 */
class OpenAIApiClient {
public:
    /**
     * @brief Construct OpenAI API client
     *
     * @param ws WebSocket client for sending messages
     */
    OpenAIApiClient(WebSocketClient* ws);

    /**
     * @brief Send session.update message
     *
     * Sends a fully-formed session configuration document.
     *
     * @param sessionConfig Session update JSON document
     * @return true if sent successfully
     */
    bool sendSessionUpdate(const JsonDocument& sessionConfig);

    /**
     * @brief Build session.update JSON document
     *
     * Populates the provided JSON document with session update fields.
     *
     * @param doc Output JSON document to populate
     * @param model Model name (e.g., "gpt-4o-realtime-preview-2024-12-17")
     * @param voice Voice name (e.g., "alloy", "shimmer", "marin")
     * @param instructions System instructions for the model
     * @param vad_threshold VAD threshold (0.0-1.0, default 0.6)
     * @param vad_prefix_padding_ms Prefix padding in ms (default 300)
     * @param vad_silence_duration_ms Silence duration in ms (default 500)
     */
    void buildSessionUpdate(
        JsonDocument& doc,
        const String& model,
        const String& voice,
        const String& instructions,
        float vad_threshold = 0.6f,
        int vad_prefix_padding_ms = 300,
        int vad_silence_duration_ms = 500
    );
    
    /**
     * @brief Send session.update message
     *
     * Configures the OpenAI session with model, voice, and VAD settings.
     *
     * @param model Model name (e.g., "gpt-4o-realtime-preview-2024-12-17")
     * @param voice Voice name (e.g., "alloy", "shimmer", "marin")
     * @param instructions System instructions for the model
     * @param vad_threshold VAD threshold (0.0-1.0, default 0.6)
     * @param vad_prefix_padding_ms Prefix padding in ms (default 300)
     * @param vad_silence_duration_ms Silence duration in ms (default 500)
     * @return true if sent successfully
     */
    bool sendSessionUpdate(
        const String& model,
        const String& voice,
        const String& instructions,
        float vad_threshold = 0.6f,
        int vad_prefix_padding_ms = 300,
        int vad_silence_duration_ms = 500
    );

    /**
     * @brief Send response.create message
     *
     * Triggers the model to generate a response based on conversation history.
     *
     * @return true if sent successfully
     */
    bool sendResponseCreate();

    /**
     * @brief Send input_audio_buffer.clear message
     *
     * Clears the server-side audio buffer to prevent echo/feedback.
     *
     * @return true if sent successfully
     */
    bool sendInputAudioClear();

    /**
     * @brief Send response.cancel message
     *
     * Cancels an in-progress response generation.
     *
     * @param response_id Optional response ID to cancel (empty for current)
     * @return true if sent successfully
     */
    bool sendResponseCancel(const String& response_id = "");

    /**
     * @brief Send input_audio_buffer.append message
     *
     * Sends PCM16 audio data to the server (base64 encoded).
     *
     * @param base64_audio Base64-encoded PCM16 audio data
     * @return true if sent successfully
     */
    bool sendInputAudioAppend(const String& base64_audio);

    /**
     * @brief Send conversation.item.create message
     *
     * Sends text input to the conversation.
     *
     * @param text Text content to send
     * @return true if sent successfully
     */
    bool sendConversationItemCreate(const String& text);

    /**
     * @brief Send function_call_output for a tool result
     *
     * @param callId Tool call ID from OpenAI
     * @param result Tool result output
     * @return true if sent successfully
     */
    bool sendFunctionCallOutput(const String& callId, const String& result);

    /**
     * @brief Send response.create with custom instructions
     *
     * Triggers response generation with additional instructions for this turn.
     *
     * @param instructions Additional instructions for this response
     * @return true if sent successfully
     */
    bool sendResponseCreateWithInstructions(const String& instructions);

private:
    WebSocketClient* _ws;

    /**
     * @brief Send JSON document via WebSocket
     *
     * @param doc JsonDocument to serialize and send
     * @return true if sent successfully
     */
    bool sendJson(const JsonDocument& doc);

    /**
     * @brief Send raw text message via WebSocket
     *
     * @param message Message to send
     * @return true if sent successfully
     */
    bool sendText(const String& message);
};
