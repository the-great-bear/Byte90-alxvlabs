/**
 * McpToolManager.h
 *
 * Declarations for McpToolManager.
 */

#pragma once

#include "SystemState.h"

#include <Arduino.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class ApplicationAudio;
class McpServer;
class SystemStateManager;

/**
 * McpToolManager - MCP tool request queue and execution worker.
 */
class McpToolManager {
public:
    McpToolManager(SystemStateManager* state_manager, ApplicationAudio* audio);
    ~McpToolManager();

    void setMcpServer(McpServer* server);
    void setStateManager(SystemStateManager* state_manager);
    void setAudio(ApplicationAudio* audio);
    void setToolsListCallback(std::function<void()> callback);

    void ensureWorker();
    void enqueueRequest(const String& payload, const String& session_id, bool enter_loading);

    void enterToolLoading();
    void exitToolLoading();

private:
    static void toolTask(void* parameter);
    void runToolWorker();

    SystemStateManager* _state_manager;
    ApplicationAudio* _audio;
    McpServer* _mcp_server;
    QueueHandle_t _tool_queue;
    int _tool_loading_depth;
    SystemState _tool_loading_previous_state;
    std::function<void()> _tools_list_callback;
};
