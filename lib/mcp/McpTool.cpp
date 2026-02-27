/**
 * McpTool.cpp
 *
 * Implementation for McpTool.
 */

#include "McpTool.h"

McpTool::McpTool(const String& name,
                 const String& description,
                 const PropertyList& properties,
                 std::function<ReturnValue(PropertyList&)> callback)
    : name_(name),
      description_(description),
      properties_(properties),
      callback_(callback) {}

String McpTool::to_json() const {
    JsonDocument doc;

    doc["name"] = name_;
    doc["description"] = description_;

    if (user_only_) {
        JsonObject annotations = doc["annotations"].to<JsonObject>();
        JsonArray audience = annotations["audience"].to<JsonArray>();
        audience.add("user");
    }

    JsonObject inputSchema = doc["inputSchema"].to<JsonObject>();
    inputSchema["type"] = "object";

    // Add properties schema
    if (properties_.size() == 0) {
        inputSchema["properties"].to<JsonObject>();
    } else {
        JsonDocument propsDoc;
        DeserializationError error = deserializeJson(propsDoc, properties_.to_json());
        if (error) {
            inputSchema["properties"].to<JsonObject>();
        } else {
            inputSchema["properties"] = propsDoc.as<JsonObject>();
        }
    }

    // Add required fields
    std::vector<String> required = properties_.getRequired();
    if (!required.empty()) {
        JsonArray requiredArray = inputSchema["required"].to<JsonArray>();
        for (const auto& fieldName : required) {
            requiredArray.add(fieldName);
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String McpTool::call(PropertyList& arguments) {
    // Execute the callback
    ReturnValue returnValue = callback_(arguments);

    // Build the result JSON
    JsonDocument resultDoc;
    JsonArray content = resultDoc["content"].to<JsonArray>();

    if (returnValue.type == RETURN_JSON && returnValue.jsonDoc != nullptr) {
        // Special handling for JSON return values
        JsonObject item = content.add<JsonObject>();
        item["type"] = "text";

        String jsonStr;
        serializeJson(*returnValue.jsonDoc, jsonStr);
        item["text"] = jsonStr;

        // Clean up the JSON document
        delete returnValue.jsonDoc;
    } else {
        // Text return value
        JsonObject item = content.add<JsonObject>();
        item["type"] = "text";

        switch (returnValue.type) {
            case RETURN_BOOL:
                item["text"] = returnValue.boolValue ? "true" : "false";
                break;
            case RETURN_INT:
                item["text"] = String(returnValue.intValue);
                break;
            case RETURN_STRING:
                item["text"] = returnValue.stringValue;
                break;
            default:
                item["text"] = "null";
                break;
        }
    }

    resultDoc["isError"] = false;

    String output;
    serializeJson(resultDoc, output);
    return output;
}
