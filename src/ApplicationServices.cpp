/**
 * ApplicationServices.cpp
 *
 * Implementation for ApplicationServices.
 */

#include "ApplicationServices.h"

#include "EventBus.h"
#include "McpToolManager.h"
#include "ProtocolManager.h"

#include <esp_log.h>

static const char* TAG = "ApplicationServices";

ApplicationServices::ApplicationServices(
    NVSStorage* storage,
    SystemStateManager*& state_manager,
    ApplicationAudio*& audio,
    McpServer*& mcp_server,
    bool& protocol_connected,
    bool& protocol_ready,
    bool& pending_listening_start,
    EventBus* event_bus
)
    : _mcp_tool_manager(new McpToolManager(state_manager, audio))
    , _protocol_manager(new ProtocolManager(
          storage,
          state_manager,
          audio,
          mcp_server,
          protocol_connected,
          protocol_ready,
          pending_listening_start,
          _mcp_tool_manager))
    , _event_bus(event_bus)
    , _connect_sub_id(0)
    , _start_listen_sub_id(0)
    , _stop_listen_sub_id(0)
    , _cancel_openai_sub_id(0)
{
    if (_event_bus) {
        _connect_sub_id = _event_bus->subscribe(EventType::CONNECT_PROTOCOL, [this](const Event&) {
            connectProtocol();
        });
        _start_listen_sub_id = _event_bus->subscribe(EventType::START_LISTENING, [this](const Event&) {
            startListening();
        });
        _stop_listen_sub_id = _event_bus->subscribe(EventType::STOP_LISTENING, [this](const Event&) {
            stopListening();
        });
        _cancel_openai_sub_id = _event_bus->subscribe(EventType::CANCEL_OPENAI_RESPONSE, [this](const Event&) {
            cancelOpenAIResponse();
        });
    }
    if (!_mcp_tool_manager || !_protocol_manager) {
        ESP_LOGW(TAG, "Service manager allocation incomplete");
    }
}

ApplicationServices::~ApplicationServices() {
    if (_event_bus) {
        if (_connect_sub_id > 0) {
            _event_bus->unsubscribe(EventType::CONNECT_PROTOCOL, _connect_sub_id);
        }
        if (_start_listen_sub_id > 0) {
            _event_bus->unsubscribe(EventType::START_LISTENING, _start_listen_sub_id);
        }
        if (_stop_listen_sub_id > 0) {
            _event_bus->unsubscribe(EventType::STOP_LISTENING, _stop_listen_sub_id);
        }
        if (_cancel_openai_sub_id > 0) {
            _event_bus->unsubscribe(EventType::CANCEL_OPENAI_RESPONSE, _cancel_openai_sub_id);
        }
    }
    delete _protocol_manager;
    delete _mcp_tool_manager;
}

void ApplicationServices::initializeProtocol() {
    if (_protocol_manager) {
        _protocol_manager->initializeProtocol();
    }
}

void ApplicationServices::setMcpServer(McpServer* server) {
    if (_protocol_manager) {
        _protocol_manager->setMcpServer(server);
    }
    if (_mcp_tool_manager) {
        _mcp_tool_manager->setMcpServer(server);
    }
}

void ApplicationServices::setEmotionCallback(std::function<void(const String&)> callback) {
    if (_protocol_manager) {
        _protocol_manager->setEmotionCallback(callback);
    }
}

void ApplicationServices::connectProtocol() {
    if (_protocol_manager) {
        _protocol_manager->connectProtocol();
    }
}

void ApplicationServices::handleIncomingJson(const cJSON* root) {
    if (_protocol_manager) {
        _protocol_manager->handleIncomingJson(root);
    }
}

void ApplicationServices::handleIncomingAudio(AudioStreamPacket* packet) {
    if (_protocol_manager) {
        _protocol_manager->handleIncomingAudio(packet);
    }
}

void ApplicationServices::startListening() {
    if (_protocol_manager) {
        _protocol_manager->startListening();
    }
}

void ApplicationServices::stopListening() {
    if (_protocol_manager) {
        _protocol_manager->stopListening();
    }
}

void ApplicationServices::cancelOpenAIResponse() {
    if (_protocol_manager) {
        _protocol_manager->cancelOpenAIResponse();
    }
}

void ApplicationServices::performDisconnect(bool playSound) {
    if (_protocol_manager) {
        _protocol_manager->performDisconnect(playSound);
    }
}

void ApplicationServices::updateAudioTransmission() {
    if (_protocol_manager) {
        _protocol_manager->updateAudioTransmission();
    }
}

void ApplicationServices::poll() {
    if (_protocol_manager) {
        _protocol_manager->poll();
    }
}

void ApplicationServices::getUiProtocolState(WebSocketClient*& ws_client, bool& hello_received) const {
    if (_protocol_manager) {
        _protocol_manager->getUiProtocolState(ws_client, hello_received);
        return;
    }
    ws_client = nullptr;
    hello_received = false;
}

WebSocketClient* ApplicationServices::getWebsocketClient() const {
    if (_protocol_manager) {
        return _protocol_manager->getWebsocketClient();
    }
    return nullptr;
}
