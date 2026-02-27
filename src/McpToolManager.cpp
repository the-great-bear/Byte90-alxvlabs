/**
 * McpToolManager.cpp
 *
 * Implementation for McpToolManager.
 */

#include "McpToolManager.h"

#include "ApplicationAudio.h"
#include "McpServer.h"
#include "TaskManager.h"
#include "HapticsManager.h"

#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "McpToolManager";

namespace {
struct McpToolRequest {
    String payload;
    String session_id;
};
} // namespace

McpToolManager::McpToolManager(SystemStateManager* state_manager, ApplicationAudio* audio)
    : _state_manager(state_manager)
    , _audio(audio)
    , _mcp_server(nullptr)
    , _tool_queue(nullptr)
    , _tool_loading_depth(0)
    , _tool_loading_previous_state(SYSTEM_STATE_IDLE) {
}

McpToolManager::~McpToolManager() {
    TaskManager::instance().stopTask("mcp_tool");
    if (_tool_queue) {
        vQueueDelete(_tool_queue);
        _tool_queue = nullptr;
    }
}

void McpToolManager::setMcpServer(McpServer* server) {
    _mcp_server = server;
}

void McpToolManager::setStateManager(SystemStateManager* state_manager) {
    _state_manager = state_manager;
}

void McpToolManager::setAudio(ApplicationAudio* audio) {
    _audio = audio;
}

void McpToolManager::setToolsListCallback(std::function<void()> callback) {
    _tools_list_callback = callback;
}

void McpToolManager::ensureWorker() {
    if (!_tool_queue) {
        _tool_queue = xQueueCreate(16, sizeof(McpToolRequest*));
        if (_tool_queue) {
            ESP_LOGI(TAG, "MCP tool queue created (size=16)");
        } else {
            ESP_LOGE(TAG, "Failed to create MCP tool queue (size=16)");
        }
    }

    if (_tool_queue && !TaskManager::instance().isTaskActive("mcp_tool")) {
        bool created = TaskManager::instance().createTask(
            "mcp_tool",
            "McpToolManager",
            toolTask,
            this,
            1,                      // Priority
            1,                      // Core 1
            8192,                   // 8KB stack
            CleanupPattern::FORCE_DELETE,
            "MCP tool request processing"
        );
        if (!created) {
            ESP_LOGE(TAG, "Failed to start MCP tool worker");
        }
    }
}

void McpToolManager::enqueueRequest(const String& payload, const String& session_id, bool enter_loading) {
    if (!_tool_queue) {
        ESP_LOGW(TAG, "MCP tool queue unavailable, dropping request");
        return;
    }

    McpToolRequest* request = new McpToolRequest{
        payload,
        session_id
    };

    if (xQueueSend(_tool_queue, &request, 0) == pdTRUE) {
        if (enter_loading) {
            enterToolLoading();
        }
    } else {
        delete request;
        ESP_LOGW(TAG, "MCP tool queue full, dropping request");
    }
}

void McpToolManager::enterToolLoading() {
    if (!_state_manager) {
        return;
    }
    if (_tool_loading_depth == 0) {
        _tool_loading_previous_state = _state_manager->getState();
        _state_manager->setState(SYSTEM_STATE_LOADING);
    }
    _tool_loading_depth++;
}

void McpToolManager::exitToolLoading() {
    if (_tool_loading_depth <= 0) {
        return;
    }
    _tool_loading_depth--;
    if (_tool_loading_depth == 0 && _state_manager &&
        _state_manager->getState() == SYSTEM_STATE_LOADING) {
        _state_manager->setState(_tool_loading_previous_state);
        if (_tool_loading_previous_state == SYSTEM_STATE_SPEAKING && _audio) {
            _audio->playSoundWithHaptic("/sounds/speaking.mp3", HapticsManager::HAPTIC_EVENT_SPEAKING);
        }
    }
}

void McpToolManager::toolTask(void* parameter) {
    McpToolManager* manager = static_cast<McpToolManager*>(parameter);
    if (manager) {
        manager->runToolWorker();
    }
    TaskManager::instance().markTaskStopped("mcp_tool");
    vTaskDelete(nullptr);
}

void McpToolManager::runToolWorker() {
    while (true) {
        McpToolRequest* request = nullptr;
        if (_tool_queue &&
            xQueueReceive(_tool_queue, &request, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (!request) {
                continue;
            }

            ESP_LOGI(TAG, "MCP tool worker processing");
            JsonDocument request_doc;
            deserializeJson(request_doc, request->payload);
            const char* method = request_doc["method"] | "";

            String response = _mcp_server ? _mcp_server->parseMessage(request->payload) : "";
            if (!response.isEmpty()) {
                ESP_LOGI(TAG, "MCP tool response ready");
                JsonDocument response_doc;
                DeserializationError error = deserializeJson(response_doc, response);
                if (!error) {
                    String payload_str;
                    serializeJson(response_doc, payload_str);
                    ESP_LOGI(TAG, "MCP tool response payload: %s", payload_str.c_str());
                    if (strcmp(method, "tools/list") == 0 &&
                        response_doc["result"]["tools"].is<JsonArray>()) {
                        size_t tool_count = response_doc["result"]["tools"].as<JsonArray>().size();
                        ESP_LOGI(TAG, "MCP tools/list response tools=%u", (unsigned int)tool_count);
                        if (_tools_list_callback) {
                            _tools_list_callback();
                        }
                    }
                    ESP_LOGI(TAG, "MCP tool response processed");
                } else {
                    ESP_LOGE(TAG, "❌ Failed to parse MCP response: %s", error.c_str());
                }
            }

            delete request;
            exitToolLoading();
        }
    }
}
