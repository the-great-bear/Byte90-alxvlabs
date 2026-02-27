/**
 * ApplicationServices.h
 *
 * Declarations for ApplicationServices.
 */

#pragma once

// Project includes
#include "ApplicationAudio.h"
#include "McpServer.h"
#include "DeviceConfig.h"
#include "SystemState.h"

// Third-party includes
#include <cJSON.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Forward declarations
/**
 * @brief WifiManager.
 */
class WifiManager;
/**
 * @brief EventBus.
 */
class EventBus;
/**
 * @brief WebSocketClient.
 */
class WebSocketClient;
/**
 * @brief McpToolManager.
 */
class McpToolManager;
/**
 * @brief ProtocolManager.
 */
class ProtocolManager;
/**
 * @brief NVSStorage.
 */
class NVSStorage;

/**
 * ApplicationServices - Service layer management
 *
 * Features:
 * - Protocol connection management (OpenAI realtime)
 * - Incoming control/data routing for audio and MCP
 *
 * Architecture:
 * - Owns protocol layer components
 * - References to global state (no ownership)
 */
class ApplicationServices {
public:
    /**
     * @brief Construct services handler
     *
     * @param storage NVS storage instance
     * @param state_manager System state manager
     * @param audio Audio subsystem
     * @param mcp_server MCP server
     * @param protocol_connected Protocol connected flag reference
     * @param protocol_ready Protocol ready flag reference
     * @param pending_listening_start Pending listening start flag reference
     */
    ApplicationServices(
        NVSStorage* storage,
        SystemStateManager*& state_manager,
        ApplicationAudio*& audio,
        McpServer*& mcp_server,
        bool& protocol_connected,
        bool& protocol_ready,
        bool& pending_listening_start,
        EventBus* event_bus
    );

    /**
     * @brief Destroy services handler and cleanup resources
     */
    ~ApplicationServices();

    /**
     * @brief Initialize protocol and setup callbacks
     */
    void initializeProtocol();

    /**
     * @brief Update MCP server reference and wire OpenAI tools
     *
     * @param server MCP server instance
     */
    void setMcpServer(McpServer* server);

    /**
     * @brief Register callback for emotion events
     *
     * @param callback Function invoked with emotion name
     */
    void setEmotionCallback(std::function<void(const String&)> callback);

    /**
     * @brief Connect to protocol server
     */
    void connectProtocol();

    /**
     * @brief Handle incoming JSON message
     *
     * @param root Parsed cJSON root object
     */
    void handleIncomingJson(const cJSON* root);

    /**
     * @brief Handle incoming audio packet
     *
     * @param packet Audio stream packet
     */
    void handleIncomingAudio(AudioStreamPacket* packet);

    /**
     * @brief Start listening mode
     */
    void startListening();

    /**
     * @brief Stop listening mode
     */
    void stopListening();
    void cancelOpenAIResponse();

    /**
     * @brief Perform disconnect cleanup
     *
     * @param playSound true to play disconnect sound
     */
    void performDisconnect(bool playSound);

    /**
     * @brief Update audio transmission state
     */
    void updateAudioTransmission();

    void getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const;
    WebSocketClient* getWebsocketClient() const;
    void poll();

private:
    McpToolManager* _mcp_tool_manager;
    ProtocolManager* _protocol_manager;
    EventBus* _event_bus;
    int _connect_sub_id;
    int _start_listen_sub_id;
    int _stop_listen_sub_id;
    int _cancel_openai_sub_id;
};
