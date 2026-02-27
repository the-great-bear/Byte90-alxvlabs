/**
 * McpServer.cpp
 *
 * Implementation for McpServer.
 */

#include "McpServer.h"
#include <esp_log.h>

static const char* TAG = "McpServer";

#ifndef BOARD_NAME
#define BOARD_NAME "BYTE-90"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

McpServer::McpServer()
    : ws_client_(nullptr)
    , audio_codec_(nullptr)
    , audio_service_(nullptr)
    , power_manager_(nullptr)
    , display_(nullptr)
    , storage_(nullptr)
    , ui_(nullptr)
    , timer_manager_(nullptr)
    , effects_manager_(nullptr)
    , gif_manager_(nullptr)
{}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::addTool(McpTool* tool) {
    // Prevent adding duplicate tools
    for (const auto& t : tools_) {
        if (t->name() == tool->name()) {
            ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
            return;
        }
    }

    ESP_LOGD(TAG, "Add tool: %s", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::addTool(const String& name,
                        const String& description,
                        const PropertyList& properties,
                        std::function<ReturnValue(PropertyList&)> callback) {
    addTool(new McpTool(name, description, properties, callback));
}

void McpServer::addUserOnlyTool(const String& name,
                                const String& description,
                                const PropertyList& properties,
                                std::function<ReturnValue(PropertyList&)> callback) {
    auto* tool = new McpTool(name, description, properties, callback);
    tool->setUserOnly(true);
    addTool(tool);
}

String McpServer::parseMessage(const String& message) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", error.c_str());
        return "";
    }

    return parseMessage(doc);
}

String McpServer::parseMessage(const JsonDocument& doc) {
    // Log the incoming request
    String requestJson;
    serializeJson(doc, requestJson);
    ESP_LOGD(TAG, "Request received: %s", requestJson.c_str());

    // Check JSONRPC version
    if (!doc["jsonrpc"].is<const char*>() ||
        strcmp(doc["jsonrpc"], "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version");
        return "";
    }

    // Check method
    if (!doc["method"].is<const char*>()) {
        ESP_LOGE(TAG, "Missing method");
        return "";
    }

    String method = doc["method"].as<String>();

    // Ignore notifications
    if (method.startsWith("notifications")) {
        return "";
    }

    // Check id
    if (!doc["id"].is<int>()) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method.c_str());
        return "";
    }

    int id = doc["id"].as<int>();

    String output;

    // Route to method handlers - pass the doc["params"] as JsonObjectConst
    if (method == "initialize") {
        JsonObjectConst params = doc["params"].as<JsonObjectConst>();
        output = handleInitialize(id, params);
    } else if (method == "tools/list") {
        JsonObjectConst params = doc["params"].as<JsonObjectConst>();
        output = handleToolsList(id, params);
    } else if (method == "tools/call") {
        JsonObjectConst params = doc["params"].as<JsonObjectConst>();
        output = handleToolsCall(id, params);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method.c_str());
        replyError(id, JSON_RPC_ERROR_METHOD_NOT_FOUND, "Method not implemented: " + method, output);
    }

    return output;
}

String McpServer::handleInitialize(int id, const JsonObjectConst& payload) {

    JsonDocument resultDoc;
    resultDoc["protocolVersion"] = "2024-11-05";

    JsonObject capabilities = resultDoc["capabilities"].to<JsonObject>();
    capabilities["tools"].to<JsonObject>();  // Create empty object (use .to<> to ensure it's not null)

    JsonObject serverInfo = resultDoc["serverInfo"].to<JsonObject>();
    serverInfo["name"] = BOARD_NAME;
    serverInfo["version"] = FIRMWARE_VERSION;

    String resultStr;
    serializeJson(resultDoc, resultStr);

    String output;
    replyResult(id, resultStr, output);
    return output;
}

String McpServer::handleToolsList(int id, const JsonObjectConst& payload) {
    String cursor = "";
    bool list_user_only_tools = false;

    if (!payload["cursor"].isNull() && payload["cursor"].is<const char*>()) {
        cursor = payload["cursor"].as<String>();
    }
    if (!payload["withUserTools"].isNull() && payload["withUserTools"].is<bool>()) {
        list_user_only_tools = payload["withUserTools"].as<bool>();
    }

    JsonDocument resultDoc;
    JsonArray toolsArray = resultDoc["tools"].to<JsonArray>();

    bool foundCursor = cursor.isEmpty();
    String nextCursor = "";
    size_t currentSize = 0;

    for (auto it = tools_.begin(); it != tools_.end(); ++it) {
        // Find cursor position
        if (!foundCursor) {
            if ((*it)->name() == cursor) {
                foundCursor = true;
                continue;
            }
            continue;
        }

        if (!list_user_only_tools && (*it)->isUserOnly()) {
            continue;
        }

        // Check payload size
        String toolJson = (*it)->to_json();
        if (currentSize + toolJson.length() + 30 > MCP_MAX_PAYLOAD_SIZE) {
            // Would exceed size limit, set next cursor
            nextCursor = (*it)->name();
            break;
        }

        // Add tool to array
        JsonDocument toolDoc;
        deserializeJson(toolDoc, toolJson);
        toolsArray.add(toolDoc.as<JsonObject>());

        currentSize += toolJson.length();
    }

    // Add nextCursor if pagination needed
    if (!nextCursor.isEmpty()) {
        resultDoc["nextCursor"] = nextCursor;
    }

    String resultStr;
    serializeJson(resultDoc, resultStr);

    String output;
    replyResult(id, resultStr, output);
    return output;
}

String McpServer::handleToolsCall(int id, const JsonObjectConst& payload) {
    if (payload["name"].isNull() || !payload["name"].is<const char*>()) {
        ESP_LOGE(TAG, "tools/call: Missing name");
        String output;
        replyError(id, JSON_RPC_ERROR_INVALID_PARAMS, "Missing name", output);
        return output;
    }

    String toolName = payload["name"].as<String>();

    // Find the tool
    McpTool* tool = nullptr;
    for (auto t : tools_) {
        if (t->name() == toolName) {
            tool = t;
            break;
        }
    }

    if (tool == nullptr) {
        ESP_LOGE(TAG, "Unknown tool: %s", toolName.c_str());
        String output;
        replyError(id, JSON_RPC_ERROR_METHOD_NOT_FOUND, "Unknown tool: " + toolName, output);
        return output;
    }

    // Get arguments
    JsonObjectConst toolArguments;
    if (!payload["arguments"].isNull() && payload["arguments"].is<JsonObjectConst>()) {
        toolArguments = payload["arguments"].as<JsonObjectConst>();
    }

    // Parse arguments into PropertyList
    PropertyList arguments = tool->properties();

    for (auto& argument : arguments) {
        bool found = false;

        if (!toolArguments.isNull() && !toolArguments[argument.name()].isNull()) {
            JsonVariantConst value = toolArguments[argument.name()];

            if (argument.type() == PROPERTY_TYPE_BOOLEAN && value.is<bool>()) {
                argument.setValue(value.as<bool>());
                found = true;
            } else if (argument.type() == PROPERTY_TYPE_INTEGER && value.is<int>()) {
                argument.setValue(value.as<int>());
                found = true;
            } else if (argument.type() == PROPERTY_TYPE_STRING && value.is<const char*>()) {
                argument.setValue(value.as<const char*>());
                found = true;
            }
        }

        // Check required arguments
        if (!argument.has_default_value() && !found) {
            ESP_LOGE(TAG, "Missing valid argument: %s", argument.name().c_str());
            String output;
            replyError(id, JSON_RPC_ERROR_INVALID_PARAMS, "Missing valid argument: " + argument.name(), output);
            return output;
        }
    }

    // Execute the tool
    String resultStr = tool->call(arguments);

    String output;
    replyResult(id, resultStr, output);
    return output;
}

void McpServer::replyResult(int id, const String& result, String& output) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;

    // Parse result string into JSON
    JsonDocument resultDoc;
    deserializeJson(resultDoc, result);
    doc["result"] = resultDoc.as<JsonObject>();

    serializeJson(doc, output);

    // Log the response for debugging
    ESP_LOGD(TAG, "Response sent: %s", output.c_str());
}

void McpServer::replyError(int id, int code, const String& message, String& output) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;

    JsonObject error = doc["error"].to<JsonObject>();
    error["code"] = code;
    error["message"] = message;

    serializeJson(doc, output);

    // Log the error response for debugging
    ESP_LOGW(TAG, "Error response sent: %s", output.c_str());
}
