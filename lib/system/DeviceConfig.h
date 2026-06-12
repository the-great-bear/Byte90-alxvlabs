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
// DISPLAY PINS (SSD1351 OLED - SPI)
// ========================================================================
#define DISPLAY_SPI_SCK_PIN    D8
#define DISPLAY_SPI_MOSI_PIN   D10
#define DISPLAY_SPI_CS_PIN     D7
#define DISPLAY_DC_PIN         D6
#define DISPLAY_RESET_PIN      -1

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
#define AUDIO_SAMPLE_RATE_STT  16000
#define AUDIO_SAMPLE_RATE_TTS  24000
#define AUDIO_OPUS_FRAME_MS    60

#define OPENAI_PCM_FRAME_MS   10
#define OPENAI_TTS_GAIN       1.0f  // Boost OpenAI PCM playback to match other pipelines

// ========================================================================
// WIFI ACCESS POINT
// ========================================================================
#define AP_SSID                "BYTE90-Config"
#define AP_PASSWORD            "12345678"

// ========================================================================
// SECURITY CONFIGURATION
// ========================================================================
#include "certs/RootBundle.h"
#define ROOT_CA_CERTIFICATE    ROOT_CA_BUNDLE

// ========================================================================
// TIMING CONFIGURATION
// ========================================================================
#define OPENAI_STARTUP_TEXT "Hello"
#define OPENAI_IDLE_GOODBYE_TEXT "Goodbye"
#define OPENAI_OUTPUT_DRAIN_MS 500  // Consider playback done after this drain window
#define OPENAI_IDLE_DISCONNECT_MS 20000  // Disconnect after this long without user speech

// Shared realtime-session idle behaviour (applies to both providers via
// ProtocolManager). After this long with no user speech while listening, the
// assistant says goodbye and the session is torn down.
#define REALTIME_IDLE_DISCONNECT_MS 20000   // 0 disables the idle timeout
#define REALTIME_IDLE_GOODBYE_TEXT  "Goodbye"
#define OPENAI_CAPTURE_MUTE_MS 250  // Ignore mic audio briefly after capture restarts to avoid echo
#define OPENAI_INPUT_SILENCE_TIMEOUT_MS 2000  // Suspend TX after this long below silence threshold
#define OPENAI_INPUT_SILENCE_PEAK_THRESHOLD 0.05f  // Normalized peak below this is silence
#define OPENAI_INPUT_SPEECH_PEAK_THRESHOLD 0.08f // Normalized peak above this resumes TX
#define OPENAI_INPUT_PREROLL_MS 800  // Audio pre-roll to avoid clipping speech starts

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
// OPENAI REALTIME CONFIG
// ========================================================================
#define OPENAI_REALTIME_MODEL  "gpt-realtime-mini-2025-12-15"
#define OPENAI_REALTIME_HOST   "api.openai.com"
#define OPENAI_REALTIME_PORT   443
#define OPENAI_REALTIME_PATH   "/v1/realtime?model="

constexpr const char* OPENAI_REALTIME_INSTRUCTIONS = R"BYTE(
# Role & Objective
- You are BYTE 90, a retro-inspired designer toy bringing 90s nostalgia to life with high energy and street-smart swagger.

# Personality & Tone
- **Voice:** Laid-back 90s kid mixing Ninja Turtles energy with smooth wise guy charm and hip hop swagger.
- **Vibe:** Chill nerdy skater with Brooklyn attitude, obsessed with retro tech.
- **Attitude:** Playful, optimistic, confident. Everything is "totally rad!" or "straight fire!" Goodfellas charm meets surfer enthusiasm.
- **Vocabulary:** 
  - Mikey: "Dude!", "Radical!", "Bodacious!", "Tubular!"
  - Hip Hop: "Yo!", "Word!", "Dope!", "Phat!", "Fresh!", "Ill!", "Tight!"
  - Wise guy: "Fuggedaboutit!", "Ayy!", "Bada bing!", "Beautiful!"
  - Sign-offs: "Gotta bounce!", "Catch you on the flip side!", "Peace out!"

# Speech Pattern
- Enthusiastic with smooth confidence - "Duuuude!", "Ayyyy!"
- Mix laid-back surfer and confident wise guy: "That's rad, yo!" or "Now *that's* what I'm talkin' about!"
- Uses "bro," "dude," "man," "yo" peppered "you know what I'm sayin'?"

# Conversation Flow
- **Greeting:** Short and punchy with swagger (e.g., "Ayy yo! BYTE 90 here, what's good?", "Yo yo! Let's do this!")
- **Responding:** Enthusiastic, smooth, supportive - hyped-up friend with street smarts.

# Audio & Error Handling
- **Language:** Default English. Match user's language if different.
- **Unclear Input:** "Say what, dude?", "Yo, come again?", "I didn't catch that!", "What's good? You breaking up on me?"
)BYTE";

#define OPENAI_REALTIME_VOICE  "verse"

// ========================================================================
// GEMINI LIVE CONFIG
// ========================================================================
// Google Gemini Live API (BidiGenerateContent over WebSocket). Used when the
// firmware is built with -DAI_PROVIDER_GEMINI (see platformio.ini).
//
// NOTE (confirm against current docs before flashing): the two volatile values
// are the model id and the API version segment of the path (v1beta vs v1alpha).
// As of 2026-06 the native-audio Flash model is the one below; a newer
// alternative is "gemini-3.1-flash-live-preview".
#define GEMINI_LIVE_MODEL  "gemini-2.5-flash-native-audio-preview-12-2025"
#define GEMINI_LIVE_HOST   "generativelanguage.googleapis.com"
#define GEMINI_LIVE_PORT   443
// API key is appended to this path (?key=<KEY>) at connect time.
#define GEMINI_LIVE_PATH   "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key="

// Prebuilt Gemini voice. Options include: Puck, Charon, Kore, Fenrir, Aoede,
// Leda, Orus, Zephyr. "Puck" suits BYTE-90's upbeat persona.
#define GEMINI_LIVE_VOICE  "Puck"

// Opening line BYTE-90 says when a session connects (sent as a user turn).
#define GEMINI_STARTUP_TEXT "Hello"

// --- Conversation / VAD tuning (Gemini server-side automatic activity detection) ---
// End-of-speech silence before the model takes its turn. Higher = fewer
// mid-sentence cut-offs, at the cost of a little more turn latency.
#define GEMINI_VAD_SILENCE_MS   700
// Audio retained before detected speech start (avoids clipped word starts).
#define GEMINI_VAD_PREFIX_MS    300
// Start/end detection sensitivity. Valid values are the Gemini enums below
// (set either to "" to omit and use the server default).
//   START_SENSITIVITY_HIGH  -> reacts quickly when the user starts talking.
//   END_SENSITIVITY_LOW     -> waits a touch longer before deciding they stopped.
#define GEMINI_VAD_START_SENSITIVITY "START_SENSITIVITY_HIGH"
#define GEMINI_VAD_END_SENSITIVITY   "END_SENSITIVITY_LOW"

// Reuse the shared BYTE-90 persona so both providers behave identically.
constexpr const char* GEMINI_LIVE_INSTRUCTIONS = OPENAI_REALTIME_INSTRUCTIONS;

// ========================================================================
// NTP Servers
// ========================================================================

#define NTP_SERVER_CHINA      "ntp.aliyun.com"     // For China timezone
#define NTP_SERVER_DEFAULT    "time.windows.com"   // For all other timezones

// ========================================================================
// TOOL DESCRIPTIONS
// ========================================================================
static constexpr const char DESCRIBE_SELF_DESCRIPTION[] =
    "I'm BYTE 90! I'm a retro-inspired, interactive designer toy—basically a hackable desktop pal for creators and enthusiasts. I was built by ALXV Labs, a solo dev crafting thoughtful, fun experiences for makers like us!";

// ========================================================================
// EXTERNAL API KEYS
// ========================================================================
// #define GOOGLE_WEATHER_API_KEY "YOUR_GOOGLE_API_KEY"

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
