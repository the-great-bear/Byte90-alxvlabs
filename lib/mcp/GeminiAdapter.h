/**
 * GeminiAdapter.h
 *
 * Bridges the gap between Gemini Live function calling and the local McpServer.
 *
 * Mirrors OpenAiAdapter, but emits Gemini "functionDeclarations" (an OpenAPI
 * 3.0 Schema subset where Type names are UPPERCASE: OBJECT, STRING, INTEGER,
 * BOOLEAN, NUMBER) instead of OpenAI function tool JSON.
 */

#ifndef GEMINI_ADAPTER_H
#define GEMINI_ADAPTER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include "McpServer.h"

class GeminiAdapter {
public:
    /**
     * @param server Pointer to the existing McpServer instance containing tools.
     */
    GeminiAdapter(McpServer* server);

    /**
     * Populates a JsonArray with the function declarations of all registered
     * tools in the format Gemini expects.
     *
     * @param outputDeclarations Reference to the "functionDeclarations" array
     *                           inside setup.tools[0].
     */
    void generateSchema(JsonArray& outputDeclarations);

    /**
     * Executes a tool based on Gemini's function call.
     *
     * @param toolName The (sanitized) function name reported by Gemini.
     * @param jsonArguments The arguments serialized as a JSON object string.
     * @return A JSON string containing the result or error message.
     */
    String executeTool(const String& toolName, const String& jsonArguments);

private:
    String sanitizeToolName(const String& name);

    McpServer* server_;
    std::map<String, String> tool_name_map_;
};

#endif // GEMINI_ADAPTER_H
