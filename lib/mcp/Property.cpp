/**
 * Property.cpp
 *
 * Implementation for Property.
 */

#include "Property.h"
#include <esp_log.h>

static const char* TAG = "McpProperty";

// Required field constructor
Property::Property(const String& name, PropertyType type)
    : name_(name), type_(type), has_default_value_(false), has_range_(false),
      min_value_(0), max_value_(0), intValue_(0) {}

// Optional field with default value (bool)
Property::Property(const String& name, PropertyType type, bool default_value)
    : name_(name), type_(type), has_default_value_(true), has_range_(false),
      min_value_(0), max_value_(0) {
    boolValue_ = default_value;
}

// Optional field with default value (int)
Property::Property(const String& name, PropertyType type, int default_value)
    : name_(name), type_(type), has_default_value_(true), has_range_(false),
      min_value_(0), max_value_(0) {
    intValue_ = default_value;
}

// Optional field with default value (String)
Property::Property(const String& name, PropertyType type, const String& default_value)
    : name_(name), type_(type), has_default_value_(true), has_range_(false),
      min_value_(0), max_value_(0), stringValue_(default_value), intValue_(0) {}

// Optional field with default value (const char*)
Property::Property(const String& name, PropertyType type, const char* default_value)
    : name_(name), type_(type), has_default_value_(true), has_range_(false),
      min_value_(0), max_value_(0), stringValue_(default_value), intValue_(0) {}

// Integer with range constraints
Property::Property(const String& name, PropertyType type, int min_value, int max_value)
    : name_(name), type_(type), has_default_value_(false), has_range_(true),
      min_value_(min_value), max_value_(max_value), intValue_(0) {
    if (type != PROPERTY_TYPE_INTEGER) {
        ESP_LOGE(TAG, "Range constraints only apply to integer properties: %s", name.c_str());
    }
}

// Integer with default and range constraints
Property::Property(const String& name, PropertyType type, int default_value, int min_value, int max_value)
    : name_(name), type_(type), has_default_value_(true), has_range_(true),
      min_value_(min_value), max_value_(max_value) {
    if (type != PROPERTY_TYPE_INTEGER) {
        ESP_LOGE(TAG, "Range constraints only apply to integer properties: %s", name.c_str());
        intValue_ = 0;
        return;
    }
    if (default_value < min_value || default_value > max_value) {
        ESP_LOGE(TAG, "Default value %d must be within range [%d, %d] for property: %s",
                 default_value, min_value, max_value, name.c_str());
        intValue_ = min_value;  // Fallback to min
        return;
    }
    intValue_ = default_value;
}

// Value getters
bool Property::getBoolValue() const {
    if (type_ != PROPERTY_TYPE_BOOLEAN) {
        ESP_LOGE(TAG, "Type mismatch for property %s: expected boolean", name_.c_str());
        return false;
    }
    return boolValue_;
}

int Property::getIntValue() const {
    if (type_ != PROPERTY_TYPE_INTEGER) {
        ESP_LOGE(TAG, "Type mismatch for property %s: expected integer", name_.c_str());
        return 0;
    }
    return intValue_;
}

String Property::getStringValue() const {
    if (type_ != PROPERTY_TYPE_STRING) {
        ESP_LOGE(TAG, "Type mismatch for property %s: expected string", name_.c_str());
        return "";
    }
    return stringValue_;
}

// Value setters
void Property::setValue(bool value) {
    if (type_ != PROPERTY_TYPE_BOOLEAN) {
        ESP_LOGE(TAG, "Cannot set bool value for non-boolean property: %s", name_.c_str());
        return;
    }
    boolValue_ = value;
}

void Property::setValue(int value) {
    if (type_ != PROPERTY_TYPE_INTEGER) {
        ESP_LOGE(TAG, "Cannot set int value for non-integer property: %s", name_.c_str());
        return;
    }

    // Range validation
    if (has_range_) {
        if (value < min_value_) {
            ESP_LOGE(TAG, "Value %d is below minimum %d for property: %s",
                     value, min_value_, name_.c_str());
            return;
        }
        if (value > max_value_) {
            ESP_LOGE(TAG, "Value %d exceeds maximum %d for property: %s",
                     value, max_value_, name_.c_str());
            return;
        }
    }

    intValue_ = value;
}

void Property::setValue(const String& value) {
    if (type_ != PROPERTY_TYPE_STRING) {
        ESP_LOGE(TAG, "Cannot set string value for non-string property: %s", name_.c_str());
        return;
    }
    stringValue_ = value;
}

void Property::setValue(const char* value) {
    setValue(String(value));
}

Property& Property::setDescription(const String& description) {
    description_ = description;
    return *this;
}

Property Property::withDescription(const String& description) const {
    Property copy = *this;
    copy.description_ = description;
    return copy;
}

// JSON schema generation
String Property::to_json() const {
    JsonDocument doc;

    if (type_ == PROPERTY_TYPE_BOOLEAN) {
        doc["type"] = "boolean";
        if (has_default_value_) {
            doc["default"] = boolValue_;
        }
    } else if (type_ == PROPERTY_TYPE_INTEGER) {
        doc["type"] = "integer";
        if (has_default_value_) {
            doc["default"] = intValue_;
        }
        if (has_range_) {
            doc["minimum"] = min_value_;
            doc["maximum"] = max_value_;
        }
    } else if (type_ == PROPERTY_TYPE_STRING) {
        doc["type"] = "string";
        if (has_default_value_) {
            doc["default"] = stringValue_;
        }
    }
    if (!description_.isEmpty()) {
        doc["description"] = description_;
    }

    String output;
    serializeJson(doc, output);
    return output;
}
