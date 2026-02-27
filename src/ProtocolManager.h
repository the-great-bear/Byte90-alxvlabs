/**
 * ProtocolManager.h
 *
 * Declarations for ProtocolManager.
 */

#pragma once

#include <Arduino.h>
#include <functional>

struct ProtocolConfig;
struct cJSON;
struct AudioStreamPacket;

class ApiClient;
class ApplicationAudio;
class AudioPacketSink;
class McpServer;
class McpToolManager;
class NVSStorage;
class ProtocolClient;
class SystemStateManager;
class WebSocketClient;

#include "ProtocolFactory.h"

/**
 * ProtocolManager - Protocol lifecycle and message handling.
 */
class ProtocolManager {
public:
    ProtocolManager(
        NVSStorage* storage,
        ProtocolType*& protocol,
        ProtocolConfig& protocol_config,
        SystemStateManager*& state_manager,
        ApplicationAudio*& audio,
        McpServer*& mcp_server,
        bool& protocol_connected,
        bool& protocol_ready,
        bool& pending_listening_start,
        McpToolManager* mcp_tool_manager
    );

    ~ProtocolManager();

    void initializeProtocol();
    void setMcpServer(McpServer* server);
    void setEmotionCallback(std::function<void(const String&)> callback);
    void setConfigRefreshCallback(std::function<void()> callback);
    void connectProtocol();
    void handleIncomingJson(const cJSON* root);
    void handleIncomingAudio(AudioStreamPacket* packet);
    void getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const;
    WebSocketClient* getWebsocketClient() const;
    void poll();
    void startListening();
    void startListeningWithSource(const String& source);
    void setPendingListenSource(const String& source);
    void stopListening();
    void abortResponse(const String& reason);
    void performDisconnect(bool playSound);
    void updateAudioTransmission();

    ApiClient* getApiClient() { return _api_client; }

private:
    void handleNetworkError(const String& message);

    NVSStorage* _storage;
    ProtocolType*& _protocol;
    ProtocolConfig& _protocol_config;
    SystemStateManager*& _state_manager;
    ApplicationAudio*& _audio;
    McpServer*& _mcp_server;
    bool& _protocol_connected;
    bool& _protocol_ready;
    bool& _pending_listening_start;
    McpToolManager* _mcp_tool_manager;

    ApiClient* _api_client;
    ProtocolClient* _protocol_client;
    AudioPacketSink* _audio_sink;

    bool _pending_disconnect_sound;
    bool _idle_goodbye_in_progress;
    bool _idle_disconnect_sound_started;
    uint32_t _idle_goodbye_sent_ms;
    bool _mqtt_goodbye_pending;
    bool _disconnect_in_progress;
    String _pending_listen_source;
    std::function<void(const String&)> _emotion_callback;
    std::function<void()> _config_refresh_callback;
    uint32_t _network_error_first_ms;
    uint8_t _network_error_count;
    uint32_t _audio_congestion_first_ms;
    uint32_t _audio_congestion_last_ms;
    bool _mcp_tools_ready;
};
