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
#include "ProtocolFactory.h"
#include "SystemState.h"

// Third-party includes
#include <cJSON.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Forward declarations
/**
 * @brief ProtocolConfig.
 */
struct ProtocolConfig;
/**
 * @brief WifiManager.
 */
class WifiManager;
/**
 * @brief TenclassClient.
 */
class TenclassClient;
/**
 * @brief ApiClient.
 */
class ApiClient;
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
 * @brief ProvisioningManager.
 */
class ProvisioningManager;
/**
 * @brief NVSStorage.
 */
class NVSStorage;

/**
 * ApplicationServices - Service layer management
 *
 * Features:
 * - Protocol connection management (WebSocket/MQTT)
 * - API client management (ApiClient)
 * - Incoming JSON message handling
 * - Incoming audio packet handling
 *
 * Architecture:
 * - Owns protocol layer components (protocol client, ApiClient)
 * - References to global state (no ownership)
 */
class ApplicationServices {
public:
    /**
     * @brief Construct services handler
     *
     * @param storage NVS storage instance
     * @param protocol Protocol instance
     * @param protocol_config Protocol configuration
     * @param state_manager System state manager
     * @param audio Audio subsystem
     * @param mcp_server MCP server
     * @param provisioning_client Provisioning client
     * @param wifi_client WiFi manager
     * @param protocol_connected Protocol connected flag reference
     * @param protocol_ready Protocol ready flag reference
     * @param pending_listening_start Pending listening start flag reference
     * @param config_checked Config checked flag reference
     * @param config_check_in_progress Config check in progress flag reference
     */
    ApplicationServices(
        NVSStorage* storage,
        ProtocolType*& protocol,
        ProtocolConfig& protocol_config,
        SystemStateManager*& state_manager,
        ApplicationAudio*& audio,
        McpServer*& mcp_server,
        TenclassClient*& provisioning_client,
        WifiManager*& wifi_client,
        bool& protocol_connected,
        bool& protocol_ready,
        bool& pending_listening_start,
        bool& config_checked,
        bool& config_check_in_progress,
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
     * @brief Update MCP server reference
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

    /**
     * @brief Update OTA and provisioning status
     *
     * Handles OTA configuration checks and device activation.
     */
    void updateProvisioning();
    void getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const;
    WebSocketClient* getWebsocketClient() const;
    void poll();

    /**
     * @brief Get API client instance
     *
     * @return Pointer to ApiClient
     */
    ApiClient* getApiClient();

    /**
     * @brief Set pending listen source for next listen start
     *
     * @param source Listen start source (e.g., "button")
     */
    void setPendingListenSource(const String& source);

private:
    static void configCheckTask(void* parameter);
    void runConfigCheck();
    void startConfigCheckTask();
    void enterToolLoading();
    void exitToolLoading();
    static void mcpToolTask(void* parameter);
    void runMcpToolWorker();
    void handleNetworkError(const String& message);

    McpToolManager* _mcp_tool_manager;
    ProtocolManager* _protocol_manager;
    ProvisioningManager* _provisioning_manager;
    EventBus* _event_bus;
    int _connect_sub_id;
    int _start_listen_sub_id;
    int _stop_listen_sub_id;
    int _abort_response_sub_id;
};
