/**
 * DeviceConfig.cpp
 *
 * Implementation for DeviceConfig.
 */

#include "DeviceConfig.h"
#include <esp_random.h>

String generateUUID() {
    // UUID v4 requires 16 bytes of random data
    uint8_t uuid[16];

    // Use ESP32's hardware random number generator
    esp_fill_random(uuid, sizeof(uuid));

    // Set version (version 4) and variant bits
    uuid[6] = (uuid[6] & 0x0F) | 0x40;    // Version 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80;    // Variant 1

    // Convert bytes to standard UUID string format
    char uuid_str[37];
    snprintf(uuid_str, sizeof(uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);

    return String(uuid_str);
}
