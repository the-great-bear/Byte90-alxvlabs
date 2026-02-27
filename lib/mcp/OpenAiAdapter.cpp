/**
 * OpenAiAdapter.cpp
 * * Implementation of the OpenAI Adapter.
 */

#include "OpenAiAdapter.h"
#include <esp_log.h>

static const char* TAG = "OpenAiAdapter";

OpenAiAdapter::OpenAiAdapter(McpServer* server) : server_(server) {}

static bool toolNameExists(const JsonArray& tools, const String& name) {
    for (JsonVariant tool_variant : tools) {
        JsonObject tool_obj = tool_variant.as<JsonObject>();
        if (tool_obj.isNull()) {
            continue;
        }
        const char* existing_name = tool_obj["name"];
        if (existing_name && name == existing_name) {
            return true;
        }
    }
    return false;
}

String OpenAiAdapter::sanitizeToolName(const String& name) {
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
    return sanitized;
}

void OpenAiAdapter::generateSchema(JsonArray& outputToolsArray) {
    if (!server_) return;

    // Get list of tools from the server
    const auto& tools = server_->getTools();
    tool_name_map_.clear();

    for (auto* tool : tools) {
        String openai_name = sanitizeToolName(tool->name());
        if (tool_name_map_.count(openai_name) > 0) {
            size_t suffix = 1;
            String candidate = openai_name + "_" + String(suffix);
            while (tool_name_map_.count(candidate) > 0) {
                suffix++;
                candidate = openai_name + "_" + String(suffix);
            }
            openai_name = candidate;
        }
        tool_name_map_[openai_name] = tool->name();
        if (toolNameExists(outputToolsArray, openai_name)) {
            continue;
        }

        // Create the tool object structure
        JsonObject toolObj = outputToolsArray.add<JsonObject>();
        toolObj["type"] = "function";
        toolObj["name"] = openai_name;
        toolObj["description"] = tool->description();

        // Define Parameters
        JsonObject parameters = toolObj["parameters"].to<JsonObject>();
        parameters["type"] = "object";
        parameters["additionalProperties"] = false; // "Strict Mode" for reliable parsing
        
        JsonObject props = parameters["properties"].to<JsonObject>();
        JsonArray required = parameters["required"].to<JsonArray>();

        // Iterate over tool properties
        const auto& prop_list = tool->properties();

        for (const auto& prop : prop_list) {
            JsonObject p = props[prop.name()].to<JsonObject>();
            
            // Map C++ Types to JSON Schema Types
            switch (prop.type()) {
                case PROPERTY_TYPE_INTEGER: 
                    p["type"] = "integer"; 
                    break;
                case PROPERTY_TYPE_BOOLEAN: 
                    p["type"] = "boolean"; 
                    break;
                case PROPERTY_TYPE_STRING:  
                    p["type"] = "string"; 
                    break;
                default: 
                    p["type"] = "string"; 
                    break;
            }
            
            if (prop.description().length() > 0) {
                p["description"] = prop.description();
            } else {
                p["description"] = "Parameter: " + prop.name();
            }
            if (prop.has_default_value()) {
                if (prop.type() == PROPERTY_TYPE_BOOLEAN) {
                    p["default"] = prop.getBoolValue();
                } else if (prop.type() == PROPERTY_TYPE_INTEGER) {
                    p["default"] = prop.getIntValue();
                } else if (prop.type() == PROPERTY_TYPE_STRING) {
                    p["default"] = prop.getStringValue();
                }
            }
            if (prop.type() == PROPERTY_TYPE_INTEGER && prop.has_range()) {
                p["minimum"] = prop.min_value();
                p["maximum"] = prop.max_value();
            }
            
            if (!prop.has_default_value()) {
                // In Strict Mode, mark all fields as required
                required.add(prop.name());
            }
        }
    }
}

String OpenAiAdapter::executeTool(const String& toolName, const String& jsonArguments) {
    ESP_LOGI(TAG, "Executing OpenAI tool: %s", toolName.c_str());

    if (!server_) return "{\"error\": \"Server not initialized\"}";

    // 1. Find the tool in the server registry
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

    // 2. Parse OpenAI JSON Arguments
    JsonDocument argsDoc;
    DeserializationError error = deserializeJson(argsDoc, jsonArguments);
    if (error) {
        ESP_LOGE(TAG, "JSON Parse Error: %s", error.c_str());
        return "{\"error\": \"Invalid JSON arguments\"}";
    }
    JsonObjectConst toolArguments = argsDoc.as<JsonObjectConst>();

    // 3. Map JSON arguments to C++ PropertyList
    // Create a copy of the tool's properties to fill with values
    PropertyList arguments = tool->properties();
    
    // Iterate over the arguments expected by the tool
    for (auto& argument : arguments) {
        // If the JSON contains this argument, update the value
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

    // 4. Execute the Tool Logic
    String result = tool->call(arguments);
    ESP_LOGI(TAG, "Tool Result: %s", result.c_str());
    
    return result;
}
