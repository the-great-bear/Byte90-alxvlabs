/**
 * DeviceSimulator.h
 *
 * Simulation helpers for UI testing (battery and WiFi).
 */

#pragma once

#include <Arduino.h>

// ============================================================================
// BATTERY SIMULATION
// ============================================================================
/**
 * @brief Enable simulated battery percentages for UI testing.
 */
#ifndef SIM_BATTERY_ENABLE
#define SIM_BATTERY_ENABLE 0
#endif

/**
 * @brief Starting battery percentage for simulation.
 */
#ifndef SIM_BATTERY_START_PERCENT
#define SIM_BATTERY_START_PERCENT 100
#endif

/**
 * @brief Ending battery percentage for simulation.
 */
#ifndef SIM_BATTERY_END_PERCENT
#define SIM_BATTERY_END_PERCENT 0
#endif

/**
 * @brief Step size used per simulation tick.
 */
#ifndef SIM_BATTERY_STEP
#define SIM_BATTERY_STEP 5
#endif

/**
 * @brief Interval in milliseconds between simulation steps.
 */
#ifndef SIM_BATTERY_INTERVAL_MS
#define SIM_BATTERY_INTERVAL_MS 2000
#endif

/**
 * @brief Simulated battery connection state (1 = connected).
 */
#ifndef SIM_BATTERY_CONNECTED
#define SIM_BATTERY_CONNECTED 1
#endif

// ============================================================================
// WIFI SIMULATION
// ============================================================================
// SIM_WIFI_MODE:
// 0 = disabled, 1 = always disconnected, 2 = always connected, 3 = cycle
/**
 * @brief Enable simulated WiFi connectivity.
 */
#ifndef SIM_WIFI_MODE
#define SIM_WIFI_MODE 0
#endif

/**
 * @brief Duration in milliseconds to stay connected in cycle mode.
 */
#ifndef SIM_WIFI_CONNECTED_MS
#define SIM_WIFI_CONNECTED_MS 60000
#endif

/**
 * @brief Duration in milliseconds to stay disconnected in cycle mode.
 */
#ifndef SIM_WIFI_DISCONNECTED_MS
#define SIM_WIFI_DISCONNECTED_MS 30000
#endif

/**
 * @brief Starting state for cycle mode (1 = connected).
 */
#ifndef SIM_WIFI_START_CONNECTED
#define SIM_WIFI_START_CONNECTED 1
#endif

// ============================================================================
// ADXL345 DEBUG
// ============================================================================
#ifndef SIM_ADXL_DEBUG
#define SIM_ADXL_DEBUG 0
#endif

// ============================================================================
// TIMER SIMULATION
// ============================================================================
// SIM_TIMER_FORMAT:
// 0 = none, 1 = seconds, 2 = minutes, 3 = hours
/**
 * @brief Enable simulated timer countdown for UI testing.
 */
#ifndef SIM_TIMER_ENABLE
#define SIM_TIMER_ENABLE 0
#endif

/**
 * @brief Simulated timer duration in seconds.
 */
#ifndef SIM_TIMER_DURATION_SECONDS
#define SIM_TIMER_DURATION_SECONDS 90
#endif

/**
 * @brief Simulated timer format (seconds/minutes/hours).
 */
#ifndef SIM_TIMER_FORMAT
#define SIM_TIMER_FORMAT 2
#endif

/**
 * @brief Restart the simulated timer after expiry.
 */
#ifndef SIM_TIMER_REPEAT
#define SIM_TIMER_REPEAT 1
#endif

/**
 * @brief Delay before restarting the simulated timer.
 */
#ifndef SIM_TIMER_RESTART_DELAY_MS
#define SIM_TIMER_RESTART_DELAY_MS 2000
#endif

namespace DeviceSimulator {

/**
 * @brief Check if battery simulation is enabled.
 */
inline bool isBatterySimEnabled()
{
#if SIM_BATTERY_ENABLE
    return true;
#else
    return false;
#endif
}

/**
 * @brief Get a simulated battery percentage.
 *
 * @param percentage Pointer to store percentage (0-100).
 * @return true if battery is considered connected, false otherwise.
 */
inline bool readBatteryPercentage(uint8_t* percentage)
{
#if SIM_BATTERY_ENABLE
    static uint32_t last_update_ms = 0;
    static uint8_t fake_percent = SIM_BATTERY_START_PERCENT;

    uint32_t now = millis();
    if (now - last_update_ms >= SIM_BATTERY_INTERVAL_MS) {
        last_update_ms = now;
        if (SIM_BATTERY_START_PERCENT >= SIM_BATTERY_END_PERCENT) {
            if (fake_percent <= (uint8_t)(SIM_BATTERY_END_PERCENT + SIM_BATTERY_STEP)) {
                fake_percent = SIM_BATTERY_START_PERCENT;
            } else {
                fake_percent = (uint8_t)(fake_percent - SIM_BATTERY_STEP);
            }
        } else {
            if ((uint8_t)(fake_percent + SIM_BATTERY_STEP) >= SIM_BATTERY_END_PERCENT) {
                fake_percent = SIM_BATTERY_START_PERCENT;
            } else {
                fake_percent = (uint8_t)(fake_percent + SIM_BATTERY_STEP);
            }
        }
    }

    if (percentage) {
        *percentage = fake_percent;
    }
    return SIM_BATTERY_CONNECTED != 0;
#else
    if (percentage) {
        *percentage = 0;
    }
    return false;
#endif
}

/**
 * @brief Check if WiFi simulation is enabled.
 */
inline bool isWifiSimEnabled()
{
#if SIM_WIFI_MODE != 0
    return true;
#else
    return false;
#endif
}

/**
 * @brief Get simulated WiFi connectivity state.
 *
 * @return true if connected, false otherwise.
 */
inline bool readWifiConnected()
{
#if SIM_WIFI_MODE == 1
    return false;
#elif SIM_WIFI_MODE == 2
    return true;
#elif SIM_WIFI_MODE == 3
    static uint32_t last_toggle_ms = 0;
    static bool connected = (SIM_WIFI_START_CONNECTED != 0);
    uint32_t now = millis();
    uint32_t interval_ms = connected ? SIM_WIFI_CONNECTED_MS : SIM_WIFI_DISCONNECTED_MS;
    if (now - last_toggle_ms >= interval_ms) {
        last_toggle_ms = now;
        connected = !connected;
    }
    return connected;
#else
    return false;
#endif
}

/**
 * @brief Check if ADXL345 debug logging is enabled.
 */
inline bool isAdxlDebugEnabled()
{
#if SIM_ADXL_DEBUG
    return true;
#else
    return false;
#endif
}

/**
 * @brief Check if timer simulation is enabled.
 */
inline bool isTimerSimEnabled()
{
#if SIM_TIMER_ENABLE
    return true;
#else
    return false;
#endif
}

/**
 * @brief Get simulated timer state.
 *
 * @param remaining_seconds Pointer to store remaining seconds.
 * @param format Pointer to store format (SIM_TIMER_FORMAT).
 * @param just_expired Pointer to store expiry edge.
 * @return true if timer is running, false otherwise.
 */
inline bool readTimerState(uint32_t* remaining_seconds, uint8_t* format, bool* just_expired)
{
#if SIM_TIMER_ENABLE
    static bool running = false;
    static uint32_t start_ms = 0;
    static uint32_t finished_ms = 0;
    static bool expired_reported = false;

    uint32_t now = millis();
    if (!running) {
        if (finished_ms == 0 ||
            (SIM_TIMER_REPEAT && (now - finished_ms) >= SIM_TIMER_RESTART_DELAY_MS)) {
            running = true;
            start_ms = now;
            finished_ms = 0;
            expired_reported = false;
        }
    }

    uint32_t remaining = 0;
    if (running) {
        uint32_t elapsed = (now - start_ms) / 1000U;
        if (elapsed >= SIM_TIMER_DURATION_SECONDS) {
            running = false;
            finished_ms = now;
        } else {
            remaining = SIM_TIMER_DURATION_SECONDS - elapsed;
        }
    }

    bool expired_now = false;
    if (!running && finished_ms != 0 && !expired_reported) {
        expired_now = true;
        expired_reported = true;
    }

    if (remaining_seconds) {
        *remaining_seconds = remaining;
    }
    if (format) {
        *format = SIM_TIMER_FORMAT;
    }
    if (just_expired) {
        *just_expired = expired_now;
    }
    return running;
#else
    if (remaining_seconds) {
        *remaining_seconds = 0;
    }
    if (format) {
        *format = 0;
    }
    if (just_expired) {
        *just_expired = false;
    }
    return false;
#endif
}

} // namespace DeviceSimulator
