/**
 * Axp2101.h
 *
 * Declarations for Axp2101.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <XPowersLib.h>
#include <functional>

// Forward declarations
/**
 * @brief I2CManager.
 */
class I2CManager;

// Type definitions
typedef std::function<void()> ButtonCallback;

/**
 * AXP2101 - Power management controller
 *
 * Features:
 * - Battery charging management
 * - Power button monitoring
 * - Button event callbacks
 *
 * Hardware: AXP2101 PMIC via I2C
 */
class AXP2101 {
public:
    /**
     * @brief Construct AXP2101 power manager instance
     *
     * @param i2c_manager Pointer to I2C manager instance
     * @param i2c_addr I2C device address
     */
    AXP2101(I2CManager* i2c_manager, uint8_t i2c_addr);

    /**
     * @brief Initialize and start the component
     *
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Check if power manager is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Update button state (call this regularly in main loop)
     */
    void updateButton();

    /**
     * @brief Register callback for button click events
     *
     * @param callback Function to call when button is clicked
     */
    void onButtonClick(ButtonCallback callback);

    /**
     * @brief Register callback for button long press events
     *
     * @param callback Function to call when button is long-pressed
     */
    void onButtonLongPress(ButtonCallback callback);

    /**
     * @brief Request PMIC shutdown
     */
    void shutdown();

    /**
     * @brief Set button debounce time
     *
     * @param ms Debounce time in milliseconds
     */
    void setDebounceTime(unsigned long ms);

    /**
     * @brief Get battery percentage
     * 
     * @param percentage Pointer to store percentage (0-100)
     * @return true if battery connected, false otherwise
     */
    bool getBatteryPercentage(uint8_t* percentage);

    /**
     * @brief Check if VBUS (USB power) is present
     */
    bool isVbusIn();

    /**
     * @brief Check if the battery is currently charging
     */
    bool isCharging();

    /**
     * @brief Clear pending IRQ status flags
     */
    void clearIrqStatus();

    /**
     * @brief Read current IRQ status flags
     * @return Bitmask of IRQ status flags (0 if not initialized)
     */
    uint64_t getIrqStatus();

private:
    /**
     * @brief Configure battery charging parameters
     */
    void setupCharging();

    /**
     * @brief Process pending button events
     */
    void processButtonEvents();

    // Hardware interface
    XPowersAXP2101 _axp;
    I2CManager* _i2c_manager;
    uint8_t _addr;
    bool _initialized;

    // Button state
    unsigned long _debounce_time;
    unsigned long _last_debounce_time;
    bool _pending_click;
    bool _long_press_active;
    bool _ignore_next_release;
    ButtonCallback _on_click;
    ButtonCallback _on_long_press;
};
