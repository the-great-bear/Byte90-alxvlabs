/**
 * GeminiAdapter.cpp
 *
 * Implementation of the Gemini Live function-calling adapter.
 */

#include "GeminiAdapter.h"
#include <esp_log.h>

static const char* TAG = "GeminiAdapter";

GeminiAdapter::GeminiAdapter(McpServer* server) : server_(server) {}

static bool declarationNameExists(const JsonArray& declarations, const String& name) {
    for (JsonVariant decl_variant : declarations) {
        JsonObject decl_obj = decl_variant.as<JsonObject>();
        if (decl_obj.isNull()) {
            continue;
        }
        const char* existing_name = decl_obj["name"];
        if (existing_name && name == existing_name) {
            return true;
        }
    }
    return false;
}

String GeminiAdapter::sanitizeToolName(const String& name) {
    // Gemini function names must match ^[a-zA-Z_][a-zA-Z0-9_.-]*$ and be <= 64
    // chars. We conservatively allow [A-Za-z0-9_-] and map everything else
    // (including the '.' used by MCP tool ids) to '_'.
    String sanitized;
    sanitized.reserve(name.length());
    for (size_t i = 0; i < name.length(); ++i) {
        char c = name[i];
        bool allowed = (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') ||
                       c == '_' || c == '-';
        sanitized += allowed ? c : '_';
    }
    if (sanitized.length() == 0) {
        sanitized = "tool";
    }
    // Ensure it starts with a letter or underscore.
    char first = sanitized[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
        sanitized = String("_") + sanitized;
    }
    if (sanitized.length() > 64) {
        sanitized = sanitized.substring(0, 64);
    }
    return sanitized;
}

void GeminiAdapter::generateSchema(JsonArray& outputDeclarations) {
    if (!server_) return;

    const auto& tools = server_->getTools();
    tool_name_map_.clear();

    for (auto* tool : tools) {
        String gemini_name = sanitizeToolName(tool->name());
        if (tool_name_map_.count(gemini_name) > 0) {
            size_t suffix = 1;
            String candidate = gemini_name + "_" + String(suffix);
            while (tool_name_map_.count(candidate) > 0) {
                suffix++;
                candidate = gemini_name + "_" + String(suffix);
            }
            gemini_name = candidate;
        }
        tool_name_map_[gemini_name] = tool->name();
        if (declarationNameExists(outputDeclarations, gemini_name)) {
            continue;
        }

        JsonObject decl = outputDeclarations.add<JsonObject>();
        decl["name"] = gemini_name;
        decl["description"] = tool->description();

        // Gemini parameters schema (OpenAPI subset, UPPERCASE types).
        JsonObject parameters = decl["parameters"].to<JsonObject>();
        parameters["type"] = "OBJECT";

        JsonObject props = parameters["properties"].to<JsonObject>();
        JsonArray required = parameters["required"].to<JsonArray>();

        const auto& prop_list = tool->properties();
        for (const auto& prop : prop_list) {
            JsonObject p = props[prop.name()].to<JsonObject>();

            switch (prop.type()) {
                case PROPERTY_TYPE_INTEGER:
                    p["type"] = "INTEGER";
                    break;
                case PROPERTY_TYPE_BOOLEAN:
                    p["type"] = "BOOLEAN";
                    break;
                case PROPERTY_TYPE_STRING:
                default:
                    p["type"] = "STRING";
                    break;
            }

            if (prop.description().length() > 0) {
                p["description"] = prop.description();
            } else {
                p["description"] = "Parameter: " + prop.name();
            }

            if (prop.type() == PROPERTY_TYPE_INTEGER && prop.has_range()) {
                p["minimum"] = prop.min_value();
                p["maximum"] = prop.max_value();
            }

            // Parameters without a default value are required.
            if (!prop.has_default_value()) {
                required.add(prop.name());
            }
        }

        // Gemini rejects an empty "required" array on some revisions; drop it
        // when there is nothing required.
        if (required.size() == 0) {
            parameters.remove("required");
        }
    }
}

String GeminiAdapter::executeTool(const String& toolName, const String& jsonArguments) {
    ESP_LOGI(TAG, "Executing Gemini tool: %s", toolName.c_str());

    if (!server_) return "{\"error\": \"Server not initialized\"}";

    const auto& tools = server_->getTools();
    McpTool* tool = nullptr;
    String resolved_name = toolName;
    auto it = tool_name_map_.find(toolName);
    if (it != tool_name_map_.end()) {
        resolved_name = it->second;
    }
    for (auto t : tools) {
        if (t->name() == resolved_name) {
            tool = t;
            break;
        }
    }

    if (tool == nullptr) {
        ESP_LOGE(TAG, "Tool not found: %s", resolved_name.c_str());
        return "{\"error\": \"Tool not found\"}";
    }

    JsonDocument argsDoc;
    DeserializationError error = deserializeJson(argsDoc, jsonArguments);
    if (error) {
        ESP_LOGE(TAG, "JSON Parse Error: %s", error.c_str());
        return "{\"error\": \"Invalid JSON arguments\"}";
    }
    JsonObjectConst toolArguments = argsDoc.as<JsonObjectConst>();

    PropertyList arguments = tool->properties();
    for (auto& argument : arguments) {
        if (!toolArguments[argument.name()].isNull()) {
            JsonVariantConst value = toolArguments[argument.name()];

            if (argument.type() == PROPERTY_TYPE_BOOLEAN && value.is<bool>()) {
                argument.setValue(value.as<bool>());
            } else if (argument.type() == PROPERTY_TYPE_INTEGER && value.is<int>()) {
                argument.setValue(value.as<int>());
            } else if (argument.type() == PROPERTY_TYPE_STRING && value.is<const char*>()) {
                argument.setValue(value.as<const char*>());
            }
        }
    }

    String result = tool->call(arguments);
    ESP_LOGI(TAG, "Tool Result: %s", result.c_str());
    return result;
}
