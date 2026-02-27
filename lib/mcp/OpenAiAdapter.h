/**
 * OpenAiAdapter.h
 * * Bridges the gap between OpenAI Function Calling JSON and the local McpServer.
 */

#ifndef OPENAI_ADAPTER_H
#define OPENAI_ADAPTER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include "McpServer.h"

class OpenAiAdapter {
public:
    /**
     * @param server Pointer to the existing McpServer instance containing tools.
     */
    OpenAiAdapter(McpServer* server);

    /**
     * Populates a JsonArray with the schemas of all registered tools
     * in the format OpenAI expects (JSON Schema).
     * * @param outputToolsArray Reference to the "tools" array in your request JSON.
     */
    void generateSchema(JsonArray& outputToolsArray);

    /**
     * Executes a tool based on OpenAI's JSON response.
     * * @param toolName The name of the function (e.g., "self.display.set_brightness")
     * @param jsonArguments The arguments string provided by OpenAI (e.g., "{\"brightness\":50}")
     * @return A JSON string containing the result or error message.
     */
    String executeTool(const String& toolName, const String& jsonArguments);

private:
    String sanitizeToolName(const String& name);

    McpServer* server_;
    std::map<String, String> tool_name_map_;
};

#endif // OPENAI_ADAPTER_H
