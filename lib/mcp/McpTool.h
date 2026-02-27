/**
 * McpTool.h
 *
 * Declarations for McpTool.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

// Project includes
#include "PropertyList.h"

/**
 * Return value types for tool callbacks
 */
enum ReturnValueType {
    RETURN_BOOL,    // Boolean result
    RETURN_INT,     // Integer result
    RETURN_STRING,  // String result
    RETURN_JSON     // JSON object result
};

/**
 * Return value wrapper for tool execution results
 */
struct ReturnValue {
    ReturnValueType type;
    union {
        bool boolValue;
        int intValue;
    };
    String stringValue;
    JsonDocument* jsonDoc;  // Pointer to avoid copy

    /**
     * @brief Construct boolean return value
     *
     * @param value Boolean value
     */
    ReturnValue(bool value) : type(RETURN_BOOL), boolValue(value), jsonDoc(nullptr) {}

    /**
     * @brief Construct integer return value
     *
     * @param value Integer value
     */
    ReturnValue(int value) : type(RETURN_INT), intValue(value), jsonDoc(nullptr) {}

    /**
     * @brief Construct string return value
     *
     * @param value String value
     */
    ReturnValue(const String& value) : type(RETURN_STRING), stringValue(value), jsonDoc(nullptr) {}

    /**
     * @brief Construct string return value from C string
     *
     * @param value C string value
     */
    ReturnValue(const char* value) : type(RETURN_STRING), stringValue(value), jsonDoc(nullptr) {}

    /**
     * @brief Construct JSON return value
     *
     * @param doc Pointer to JSON document (ownership transferred)
     */
    ReturnValue(JsonDocument* doc) : type(RETURN_JSON), jsonDoc(doc), intValue(0) {}

    /**
     * @brief Destroy return value (note: jsonDoc ownership transferred to caller)
     */
    ~ReturnValue() {
        // Note: jsonDoc ownership is transferred, caller must delete
    }
};

/**
 * McpTool - MCP (Model Context Protocol) tool definition
 *
 * Features:
 * - Tool metadata (name, description)
 * - Input property schema
 * - Callback execution
 * - JSON schema generation
 *
 * Represents a callable function that AI assistants can invoke
 * to interact with device hardware and state.
 */
class McpTool {
public:
    /**
     * @brief Construct MCP tool
     *
     * @param name Tool name (unique identifier)
     * @param description Tool description for AI assistant
     * @param properties Input property schema
     * @param callback Function to execute when tool is called
     */
    McpTool(const String& name,
            const String& description,
            const PropertyList& properties,
            std::function<ReturnValue(PropertyList&)> callback);

    /**
     * @brief Get tool name
     *
     * @return Tool name
     */
    inline const String& name() const { return name_; }

    /**
     * @brief Get tool description
     *
     * @return Tool description
     */
    inline const String& description() const { return description_; }

    /**
     * @brief Get tool properties (const)
     *
     * @return Const reference to properties
     */
    inline const PropertyList& properties() const { return properties_; }

    /**
     * @brief Get tool properties
     *
     * @return Reference to properties
     */
    inline PropertyList& properties() { return properties_; }

    /**
     * @brief Generate JSON schema for this tool
     *
     * @return JSON schema string
     */
    String to_json() const;

    /**
     * @brief Execute the tool with given arguments
     *
     * @param arguments Input arguments matching property schema
     * @return Result string
     */
    String call(PropertyList& arguments);

    /**
     * @brief Mark this tool as user-only
     *
     * @param user_only True if user-only
     */
    inline void setUserOnly(bool user_only) { user_only_ = user_only; }

    /**
     * @brief Check if tool is user-only
     *
     * @return True if user-only
     */
    inline bool isUserOnly() const { return user_only_; }

private:
    // Tool metadata
    String name_;
    String description_;

    // Input schema
    PropertyList properties_;

    // Execution callback
    std::function<ReturnValue(PropertyList&)> callback_;

    // User-only flag
    bool user_only_ = false;
};
