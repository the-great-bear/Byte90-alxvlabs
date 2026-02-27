/**
 * Cst820Touch.h
 *
 * Declarations for Cst820Touch.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

// CST820 I2C address
#define CST820_I2C_ADDR 0x15

// CST820 Registers
#define CST820_REG_STATUS 0x00
#define CST820_REG_TOUCH_NUM 0x02
#define CST820_REG_XPOS_H 0x03
#define CST820_REG_XPOS_L 0x04
#define CST820_REG_YPOS_H 0x05
#define CST820_REG_YPOS_L 0x06
#define CST820_REG_CHIP_ID 0xA7
#define CST820_REG_FW_VERSION 0xA9
#define CST820_REG_SLEEP 0xE5
#define CST820_REG_DIS_AUTOSLEEP 0xFE

// Chip IDs
#define CST816S_CHIP_ID 0xB4
#define CST816T_CHIP_ID 0xB5
#define CST816D_CHIP_ID 0xB6
#define CST820_CHIP_ID 0xB7
#define CST716_CHIP_ID 0x20

/**
 * @brief CST820_Touch.
 */
class CST820_Touch {
public:
    // Home button callback type
    /**
     * @brief Void
     *
     * @param user_data User data pointer passed to callback
     *
     * @return Result value
     */
    typedef void (*home_button_callback_t)(void *user_data);

    CST820_Touch();

    // Initialize touch controller
    /**
     * @brief Initialize and start the component
     *
     * @param sda Numeric value
     * @param scl Numeric value
     * @param rst Rst
     * @param irq Irq
     *
     * @return true on success, false on failure
     */
    bool begin(int sda, int scl, int rst = -1, int irq = -1);

    // Get single touch point (returns true if touched)
    /**
     * @brief Get the touch
     *
     * @param x X
     * @param y Y
     *
     * @return true on success, false on failure
     */
    bool getTouch(int16_t &x, int16_t &y);

    // Check if screen is touched
    /**
     * @brief Check if touched
     *
     * @return true on success, false on failure
     */
    bool isTouched();

    // Get chip ID
    /**
     * @brief Get the chip i d
     *
     * @return Result value
     */
    uint8_t getChipID();

    // Get chip model name
    const char *getModelName();

    // Get firmware version
    /**
     * @brief Get the firmware version
     *
     * @return Result value
     */
    uint8_t getFirmwareVersion();

    // Sleep/wake
    /**
     * @brief Sleep
     */
    void sleep();
    /**
     * @brief Wakeup
     */
    void wakeup();

    // Auto-sleep control
    /**
     * @brief Disable auto sleep
     */
    void disableAutoSleep();
    /**
     * @brief Enable or disable auto sleep
     */
    void enableAutoSleep();

    // Coordinate transformations
    /**
     * @brief Set the swap x y
     *
     * @param swap true if swap, false otherwise
     */
    void setSwapXY(bool swap);
    /**
     * @brief Set the mirror x y
     *
     * @param mirror_x true if mirror_x, false otherwise
     * @param mirror_y true if mirror_y, false otherwise
     */
    void setMirrorXY(bool mirror_x, bool mirror_y);
    /**
     * @brief Set the max coordinates
     *
     * @param max_x Numeric value
     * @param max_y Numeric value
     */
    void setMaxCoordinates(uint16_t max_x, uint16_t max_y);

    // Home button (virtual button at specific coordinates)
    /**
     * @brief Set the center button coordinate
     *
     * @param x Numeric value
     * @param y Numeric value
     */
    void setCenterButtonCoordinate(int16_t x, int16_t y);
    /**
     * @brief Set the home button callback
     *
     * @param callback Callback function to invoke
     * @param user_data User data pointer passed to callback
     */
    void setHomeButtonCallback(home_button_callback_t callback, void *user_data = nullptr);

private:
    int _rst;
    int _irq;
    bool _swap_xy;
    bool _mirror_x;
    bool _mirror_y;
    uint16_t _max_x;
    uint16_t _max_y;
    uint8_t _chip_id;

    // Home button
    int16_t _center_btn_x;
    int16_t _center_btn_y;
    home_button_callback_t _home_button_callback;
    void *_user_data;

    // IRQ debouncing
    uint32_t _last_irq_time;

    // I2C helpers
    /**
     * @brief Receive or read data
     *
     * @param reg Numeric value
     *
     * @return Result value
     */
    uint8_t readRegister(uint8_t reg);
    /**
     * @brief Write data
     *
     * @param reg Numeric value
     * @param value Numeric value
     */
    void writeRegister(uint8_t reg, uint8_t value);
    /**
     * @brief Receive or read data
     *
     * @param reg Numeric value
     * @param buffer Buffer to store data
     * @param len Number of elements
     */
    void readRegisters(uint8_t reg, uint8_t *buffer, uint8_t len);
};
