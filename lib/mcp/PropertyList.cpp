/**
 * PropertyList.cpp
 *
 * Implementation for PropertyList.
 */

#include "PropertyList.h"
#include <esp_log.h>

static const char* TAG = "McpPropertyList";

PropertyList::PropertyList() {}

PropertyList::PropertyList(const std::vector<Property>& properties)
    : properties_(properties) {}

void PropertyList::addProperty(const Property& property) {
    properties_.push_back(property);
}

const Property& PropertyList::operator[](const String& name) const {
    for (const auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    // This shouldn't happen if used correctly, but we need to return something
    ESP_LOGE(TAG, "Property not found: %s", name.c_str());
    static Property dummy("", PROPERTY_TYPE_STRING);
    return dummy;
}

Property& PropertyList::operator[](const String& name) {
    for (auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    // This shouldn't happen if used correctly, but we need to return something
    ESP_LOGE(TAG, "Property not found: %s", name.c_str());
    static Property dummy("", PROPERTY_TYPE_STRING);
    return dummy;
}

std::vector<String> PropertyList::getRequired() const {
    std::vector<String> required;
    for (const auto& property : properties_) {
        if (!property.has_default_value()) {
            required.push_back(property.name());
        }
    }
    return required;
}

String PropertyList::to_json() const {
    JsonDocument doc;

    for (const auto& property : properties_) {
        // Parse the property's JSON and add it to the document
        JsonDocument propDoc;
        deserializeJson(propDoc, property.to_json());
        doc[property.name()] = propDoc.as<JsonObject>();
    }

    String output;
    serializeJson(doc, output);
    return output;
}
