/**
 * DeviceConfig.h
 *
 * Hardware configuration and PIN definitions for Byte90-Xiaozhi ESP32-S3 device.
 * Centralizes all hardware-specific settings for easy maintenance and board variants.
 *
 * Board: ESP32-S3 (Seeed Studio XIAO ESP32S3)
 * Author: Byte90 Team
 */

#pragma once

#include <Arduino.h>
#include <string>
// ========================================================================
// FEATURE FLAGS
// ========================================================================

/**
 * @brief Protocol selection flag
 *
 * Set to 0 to use WebSocket protocol (default)
 * Set to 1 to use MQTT protocol
 *
 * This flag controls which protocol configuration is parsed and stored:
 * - USE_MQTT_PROTOCOL=0: Only WebSocket config is parsed/stored
 * - USE_MQTT_PROTOCOL=1: Only MQTT config is parsed/stored
 * NOTE: for MQTT xiaozhi backend has a bug with randomly not invoking MCP tools, so WebSocket is recommended.
 */
#ifndef USE_MQTT_PROTOCOL
#define USE_MQTT_PROTOCOL 0
#endif

// ========================================================================
// DISPLAY PINS (SSD1351 OLED - SPI)
// ========================================================================
#define DISPLAY_SPI_SCK_PIN    D8
#define DISPLAY_SPI_MOSI_PIN   D10
#define DISPLAY_SPI_CS_PIN     D7
#define DISPLAY_DC_PIN         D6
#define DISPLAY_RESET_PIN      -1 // Reset pin bypass through hardware display initializer chip

#define DISPLAY_WIDTH         128
#define DISPLAY_HEIGHT        128
#define USE_DOS_BOOT_ANIMATION 1 // Enable DOS-style boot animation before normal startup UI.

// ========================================================================
// I2C PINS (AXP2101 Power Management)
// ========================================================================
#define I2C_SDA_PIN            D4
#define I2C_SCL_PIN            D5
#define AXP2101_I2C_ADDR       0x34
#define AXP2101_IRQ_PIN        D9

// ========================================================================
// AUDIO PINS
// ========================================================================
// Speaker (MAX98357 I2S)
#define AUDIO_SPEAKER_BCLK     D3
#define AUDIO_SPEAKER_LRC      D1
#define AUDIO_SPEAKER_DOUT     D2

// I2S Microphone (ICS-43434) - Full-duplex with speaker
#define AUDIO_MIC_I2S_BCLK     AUDIO_SPEAKER_BCLK  // Shared
#define AUDIO_MIC_I2S_LRC      AUDIO_SPEAKER_LRC   // Shared
#define AUDIO_MIC_I2S_DATA     D0                  // Unique

// ========================================================================
// AUDIO CONFIGURATION
// ========================================================================
// Xiaozhi server required audio sample rate
#define AUDIO_SAMPLE_RATE_STT  16000
#define AUDIO_SAMPLE_RATE_TTS  24000
#define AUDIO_OPUS_FRAME_MS    60

// ========================================================================
// WIFI ACCESS POINT
// ========================================================================
#define AP_SSID                "BYTE90-Config"
#define AP_PASSWORD            "12345678"

// ========================================================================
// OTA CONFIGURATION
// ========================================================================
#define TENCLASS_API           "https://api.tenclass.net/xiaozhi/ota/"

// ========================================================================
// SECURITY CONFIGURATION
// ========================================================================
#include "certs/TenclassCert.h"
#include "certs/RootBundle.h"

/**
 * @brief SSL/TLS certificate for api.tenclass.net
 *
 * Contains full certificate chain for secure HTTPS and WSS connections.
 * Used by both TenclassClient (HTTP API) and TenclassWebsocket (WebSocket).
 *
 * Memory: ~4.4KB flash (PROGMEM), no RAM overhead
 * Valid until: 2026-11-09
 */
#define API_CERTIFICATE        TENCLASS_API_CERT
#define ROOT_CA_CERTIFICATE    ROOT_CA_BUNDLE

// ========================================================================
// TIMING CONFIGURATION
// ========================================================================
#define GIF_INTERVAL_MS 10000
#define GIF_IDLE_INTERVAL_MS 10000
#define LIGHT_SLEEP_INTERVAL_MS 0  // 0 = wake on AXP2101 button IRQ, >0 = timer wake

// ========================================================================
// BACKOFF CONFIGURATION
// ========================================================================
#define PROTOCOL_MAX_RECONNECT_ATTEMPTS 10

struct BackoffState {
    uint32_t initial_delay_ms;
    uint32_t current_delay_ms;
    uint32_t attempt_count;
    unsigned long last_attempt_time;

    static const uint32_t DEFAULT_INITIAL_DELAY_MS = 1000;
    static const uint32_t MAX_DELAY_MS = 10000;
    static const uint32_t MULTIPLIER = 2;

    BackoffState()
        : initial_delay_ms(DEFAULT_INITIAL_DELAY_MS)
        , current_delay_ms(DEFAULT_INITIAL_DELAY_MS)
        , attempt_count(0)
        , last_attempt_time(0) {}

    void reset()
    {
        current_delay_ms = initial_delay_ms;
        attempt_count = 0;
        last_attempt_time = 0;
    }

    void increment()
    {
        attempt_count++;
        current_delay_ms = min(current_delay_ms * MULTIPLIER, MAX_DELAY_MS);
    }

    bool shouldRetry(unsigned long now)
    {
        return (now - last_attempt_time) >= current_delay_ms;
    }
};

// ========================================================================
#define NTP_SERVER_CHINA      "ntp.aliyun.com"     // For China timezone
#define NTP_SERVER_DEFAULT    "time.windows.com"   // For all other timezones

// ========================================================================
// TOOL DESCRIPTIONS
// ========================================================================
static constexpr const char DESCRIBE_SELF_DESCRIPTION[] =
    "I'm BYTE 90! I'm a retro-inspired, interactive designer toy—basically a hackable desktop pal for creators and enthusiasts. I was built by Alex at ALXV Labs, a solo dev crafting thoughtful, fun experiences for makers like us!";

// ========================================================================
// EXTERNAL API KEYS
// ========================================================================
// #define GOOGLE_WEATHER_API_KEY "_YOUR_GOOGLE_API_KEY"

// ========================================================================
// LANGUAGE CONFIGURATION
// ========================================================================
#define DEFAULT_LANGUAGE       "en-US"

// ========================================================================
// DEVICE IDENTIFICATION
// ========================================================================

/**
 * @brief Generate a new UUIDv4
 *
 * Uses ESP32 hardware random number generator to create a UUID.
 *
 * @return Random UUID in standard format (e.g., "550e8400-e29b-41d4-a716-446655440000")
 */
String generateUUID();

/**
 * @brief Get board type identifier
 *
 * @return Board type constant from build flags (BOARD_NAME)
 */
inline const char* getBoardType() {
    return BOARD_NAME;
}

/**
 * @brief Get user agent string for HTTP requests
 *
 * @return User agent with board type and firmware version
 */
String getUserAgent();
