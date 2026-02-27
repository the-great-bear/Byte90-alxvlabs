/**
 * Property.h
 *
 * Declarations for Property.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * Property types for MCP tool parameters
 */
enum PropertyType {
    PROPERTY_TYPE_BOOLEAN,   // Boolean value
    PROPERTY_TYPE_INTEGER,   // Integer value
    PROPERTY_TYPE_STRING     // String value
};

/**
 * Property - MCP tool parameter definition
 *
 * Features:
 * - Type-safe parameter definitions
 * - Optional default values
 * - Range constraints for integers
 * - JSON schema generation
 *
 * Used to define input parameters for MCP tools with validation.
 */
class Property {
public:
    /**
     * @brief Construct required property without default value
     *
     * @param name Property name
     * @param type Property type
     */
    Property(const String& name, PropertyType type);

    /**
     * @brief Construct optional boolean property with default
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_BOOLEAN)
     * @param default_value Default boolean value
     */
    Property(const String& name, PropertyType type, bool default_value);

    /**
     * @brief Construct optional integer property with default
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_INTEGER)
     * @param default_value Default integer value
     */
    Property(const String& name, PropertyType type, int default_value);

    /**
     * @brief Construct optional string property with default
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_STRING)
     * @param default_value Default string value
     */
    Property(const String& name, PropertyType type, const String& default_value);

    /**
     * @brief Construct optional string property with default (C string)
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_STRING)
     * @param default_value Default C string value
     */
    Property(const String& name, PropertyType type, const char* default_value);

    /**
     * @brief Construct required integer property with range constraints
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_INTEGER)
     * @param min_value Minimum allowed value
     * @param max_value Maximum allowed value
     */
    Property(const String& name, PropertyType type, int min_value, int max_value);

    /**
     * @brief Construct optional integer property with default and range
     *
     * @param name Property name
     * @param type Property type (must be PROPERTY_TYPE_INTEGER)
     * @param default_value Default integer value
     * @param min_value Minimum allowed value
     * @param max_value Maximum allowed value
     */
    Property(const String& name, PropertyType type, int default_value, int min_value, int max_value);

    // Getters

    /**
     * @brief Get property name
     *
     * @return Property name
     */
    inline const String& name() const { return name_; }

    /**
     * @brief Get property type
     *
     * @return Property type enum
     */
    inline PropertyType type() const { return type_; }

    /**
     * @brief Check if property has default value
     *
     * @return true if has default, false if required
     */
    inline bool has_default_value() const { return has_default_value_; }

    /**
     * @brief Check if property has range constraints
     *
     * @return true if has range, false otherwise
     */
    inline bool has_range() const { return has_range_; }

    /**
     * @brief Get minimum allowed value (for integers)
     *
     * @return Minimum value
     */
    inline int min_value() const { return min_value_; }

    /**
     * @brief Get maximum allowed value (for integers)
     *
     * @return Maximum value
     */
    inline int max_value() const { return max_value_; }

    /**
     * @brief Get property description
     *
     * @return Description string
     */
    inline const String& description() const { return description_; }

    /**
     * @brief Set property description
     *
     * @param description Description string
     * @return Reference to property
     */
    Property& setDescription(const String& description);

    /**
     * @brief Create a copy with description set
     *
     * @param description Description string
     * @return Property copy with description
     */
    Property withDescription(const String& description) const;

    // Value getters (type-specific)

    /**
     * @brief Get boolean value
     *
     * @return Boolean value
     */
    bool getBoolValue() const;

    /**
     * @brief Get integer value
     *
     * @return Integer value
     */
    int getIntValue() const;

    /**
     * @brief Get string value
     *
     * @return String value
     */
    String getStringValue() const;

    // Value setters (with validation)

    /**
     * @brief Set boolean value
     *
     * @param value Boolean value to set
     */
    void setValue(bool value);

    /**
     * @brief Set integer value (validates against range if set)
     *
     * @param value Integer value to set
     */
    void setValue(int value);

    /**
     * @brief Set string value
     *
     * @param value String value to set
     */
    void setValue(const String& value);

    /**
     * @brief Set string value from C string
     *
     * @param value C string value to set
     */
    void setValue(const char* value);

    // JSON schema generation

    /**
     * @brief Generate JSON schema representation
     *
     * @return JSON schema string
     */
    String to_json() const;

private:
    // Property metadata
    String name_;
    PropertyType type_;
    bool has_default_value_;

    // Value storage (union for memory efficiency)
    union {
        bool boolValue_;
        int intValue_;
    };
    String stringValue_;  // Separate for non-trivial type

    // Range constraints (for integers)
    bool has_range_;
    int min_value_;
    int max_value_;

    // Description (optional)
    String description_;
};
