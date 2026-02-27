/**
 * PropertyList.h
 *
 * Declarations for PropertyList.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Project includes
#include "Property.h"

/**
 * PropertyList - List of MCP tool properties
 *
 * Features:
 * - Property collection management
 * - Property access by name
 * - Iterator support
 * - JSON schema generation
 *
 * Used to define input parameters for MCP tools.
 */
class PropertyList {
public:
    /**
     * @brief Construct empty property list
     */
    PropertyList();

    /**
     * @brief Construct property list from vector
     *
     * @param properties Vector of Property objects
     */
    PropertyList(const std::vector<Property>& properties);

    /**
     * @brief Add property to the list
     *
     * @param property Property to add
     */
    void addProperty(const Property& property);

    /**
     * @brief Access property by name (const)
     *
     * @param name Property name
     * @return Const reference to property
     */
    const Property& operator[](const String& name) const;

    /**
     * @brief Access property by name
     *
     * @param name Property name
     * @return Reference to property
     */
    Property& operator[](const String& name);

    /**
     * @brief Get iterator to first property
     *
     * @return Iterator to beginning
     */
    std::vector<Property>::iterator begin() { return properties_.begin(); }

    /**
     * @brief Get iterator past last property
     *
     * @return Iterator to end
     */
    std::vector<Property>::iterator end() { return properties_.end(); }

    /**
     * @brief Get const iterator to first property
     *
     * @return Const iterator to beginning
     */
    std::vector<Property>::const_iterator begin() const { return properties_.begin(); }

    /**
     * @brief Get const iterator past last property
     *
     * @return Const iterator to end
     */
    std::vector<Property>::const_iterator end() const { return properties_.end(); }

    /**
     * @brief Get list of required property names
     *
     * @return Vector of required property names
     */
    std::vector<String> getRequired() const;

    /**
     * @brief Get number of properties in list
     *
     * @return Property count
     */
    size_t size() const { return properties_.size(); }

    /**
     * @brief Generate JSON schema representation
     *
     * @return JSON schema string
     */
    String to_json() const;

private:
    // Property storage
    std::vector<Property> properties_;
};
