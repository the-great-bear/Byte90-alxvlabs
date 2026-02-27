/**
 * McpServer.h
 *
 * Declarations for McpServer.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

// Project includes
#include "McpTool.h"
#include "NvsStorage.h"
#include "PropertyList.h"

// Forward declarations
/**
 * @brief WebSocketClient.
 */
class WebSocketClient;
/**
 * @brief AudioCodec.
 */
class AudioCodec;
/**
 * @brief AudioService.
 */
class AudioService;
/**
 * @brief AXP2101.
 */
class AXP2101;
/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351;
/**
 * @brief DigitalClockController.
 */
class DigitalClockController;
/**
 * @brief TimerManager.
 */
class TimerManager;
/**
 * @brief EffectsManager.
 */
class EffectsManager;
/**
 * @brief GifManager.
 */
class GifManager;

// Constants
#define MCP_MAX_PAYLOAD_SIZE 8000  // Maximum payload size for pagination

// JSON-RPC 2.0 standard error codes
static constexpr int JSON_RPC_ERROR_PARSE = -32700;
static constexpr int JSON_RPC_ERROR_INVALID_REQUEST = -32600;
static constexpr int JSON_RPC_ERROR_METHOD_NOT_FOUND = -32601;
static constexpr int JSON_RPC_ERROR_INVALID_PARAMS = -32602;
static constexpr int JSON_RPC_ERROR_INTERNAL = -32603;

/**
 * McpServer - Model Context Protocol server
 *
 * Features:
 * - Tool registration and management
 * - MCP message parsing and handling
 * - JSON-RPC protocol implementation
 * - Session management
 *
 * Handles MCP (Model Context Protocol) tool registration and message parsing.
 * Allows AI assistants to discover and invoke device capabilities.
 *
 * Uses simple instantiation pattern for clarity and predictability.
 */
class McpServer {
public:
    /**
     * @brief Construct MCP server instance
     */
    McpServer();

    /**
     * @brief Destroy MCP server and cleanup resources
     */
    ~McpServer();

    // Tool registration

    /**
     * @brief Add tool to server
     *
     * @param tool Pointer to McpTool (server takes ownership)
     */
    void addTool(McpTool* tool);

    /**
     * @brief Add tool to server (creates tool internally)
     *
     * @param name Tool name (unique identifier)
     * @param description Tool description for AI assistant
     * @param properties Input property schema
     * @param callback Function to execute when tool is called
     */
    void addTool(const String& name,
                 const String& description,
                 const PropertyList& properties,
                 std::function<ReturnValue(PropertyList&)> callback);

    void addUserOnlyTool(const String& name,
                         const String& description,
                         const PropertyList& properties,
                         std::function<ReturnValue(PropertyList&)> callback);

    // Message handling

    /**
     * @brief Parse and handle MCP message from JSON document
     *
     * @param doc Parsed JSON document
     * @return JSON-RPC response string
     */
    String parseMessage(const JsonDocument& doc);

    /**
     * @brief Parse and handle MCP message from string
     *
     * @param message JSON-RPC message string
     * @return JSON-RPC response string
     */
    String parseMessage(const String& message);

    // Session management

    /**
     * @brief Set the session ID (called from WebSocket handler)
     *
     * @param sessionId Session identifier
     */
    void setSessionId(const String& sessionId) { session_id_ = sessionId; }

    /**
     * @brief Get the current session ID
     *
     * @return Session identifier
     */
    const String& getSessionId() const { return session_id_; }

    // Component dependency injection

    /**
     * @brief Set component dependencies for MCP tools
     *
     * @param wsClient WebSocket client instance
     * @param audioCodec Audio codec instance
     * @param audioService Audio service instance
     * @param powerManager Power manager instance
     * @param display Display instance
     */
    void setComponents(WebSocketClient* wsClient,
                      AudioCodec* audioCodec,
                      AudioService* audioService,
                      AXP2101* powerManager,
                      ArduinoSSD1351* display = nullptr,
                      NVSStorage* storage = nullptr,
                      DigitalClockController* ui = nullptr,
                      TimerManager* timer_manager = nullptr) {
        ws_client_ = wsClient;
        audio_codec_ = audioCodec;
        audio_service_ = audioService;
        power_manager_ = powerManager;
        display_ = display;
        storage_ = storage;
        ui_ = ui;
        timer_manager_ = timer_manager;
    }

    /**
     * @brief Get WebSocket client
     */
    WebSocketClient* getWsClient() const { return ws_client_; }

    /**
     * @brief Get audio codec
     */
    AudioCodec* getAudioCodec() const { return audio_codec_; }

    /**
     * @brief Get audio service
     */
    AudioService* getAudioService() const { return audio_service_; }

    /**
     * @brief Get power manager
     */
    AXP2101* getPowerManager() const { return power_manager_; }

    /**
     * @brief Get display
     */
    ArduinoSSD1351* getDisplay() const { return display_; }
    NVSStorage* getStorage() const { return storage_; }
    DigitalClockController* getUi() const { return ui_; }
    TimerManager* getTimerManager() const { return timer_manager_; }
    EffectsManager* getEffectsManager() const { return effects_manager_; }
    GifManager* getGifManager() const { return gif_manager_; }
    
    /**
     * @brief Get all registered tools.
     */
    const std::vector<McpTool*>& getTools() const { return tools_; }

    void setEffectsManager(EffectsManager* effects_manager) {
        effects_manager_ = effects_manager;
    }
    void setGifManager(GifManager* gif_manager) {
        gif_manager_ = gif_manager;
    }

private:
    /**
     * @brief Handle MCP initialize request
     *
     * @param id JSON-RPC request ID
     * @param payload Request payload object
     * @return JSON-RPC response string
     */
    String handleInitialize(int id, const JsonObjectConst& payload);

    /**
     * @brief Handle tools/list request
     *
     * @param id JSON-RPC request ID
     * @param payload Request payload object
     * @return JSON-RPC response string
     */
    String handleToolsList(int id, const JsonObjectConst& payload);

    /**
     * @brief Handle tools/call request
     *
     * @param id JSON-RPC request ID
     * @param payload Request payload object
     * @return JSON-RPC response string
     */
    String handleToolsCall(int id, const JsonObjectConst& payload);

    /**
     * @brief Build JSON-RPC success response
     *
     * @param id Request ID
     * @param result Result data string
     * @param output Output string to append response to
     */
    void replyResult(int id, const String& result, String& output);

    /**
     * @brief Build JSON-RPC error response
     *
     * @param id Request ID
     * @param message Error message
     * @param output Output string to append response to
     */
    void replyError(int id, int code, const String& message, String& output);

    // Registered tools
    std::vector<McpTool*> tools_;

    // Session state
    String session_id_;

    // Component dependencies (not owned)
    WebSocketClient* ws_client_;
    AudioCodec* audio_codec_;
    AudioService* audio_service_;
    AXP2101* power_manager_;
    ArduinoSSD1351* display_;
    NVSStorage* storage_;
    DigitalClockController* ui_;
    TimerManager* timer_manager_;
    EffectsManager* effects_manager_;
    GifManager* gif_manager_;
};
