/**
 * ArduinoSSD1351.h
 *
 * Declarations for ArduinoSSD1351.
 */

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>

#define DISPLAY_BRIGHTNESS_DIM     0x00
#define DISPLAY_BRIGHTNESS_LOW     0x02
#define DISPLAY_BRIGHTNESS_MEDIUM  0x05
#define DISPLAY_BRIGHTNESS_HIGH    0x07
#define DISPLAY_BRIGHTNESS_FULL    0x0F

// RGB565 color constants
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_RED     0xF800
#define COLOR_ORANGE  0xFD20

/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351 {
public:
    ArduinoSSD1351(int8_t cs_pin, int8_t dc_pin, int8_t rst_pin,
                   int8_t sclk_pin, int8_t mosi_pin);
    
    /**
     * @brief Initialize the display
     */
    bool begin();
    
    /**
     * @brief Set display brightness level
     * @param brightness Level 0-15 (use DISPLAY_BRIGHTNESS_* constants)
     */
    void setBrightness(uint8_t brightness);
    
    /**
     * @brief Set display brightness as percentage
     * @param percent Brightness level 0-100%
     */
    void setBrightnessPercent(uint8_t percent);
    
    /**
     * @brief Get current brightness as percentage
     */
    uint8_t getBrightnessPercent() const;
    
    // Direct access to Adafruit display for all other functions
    Adafruit_SSD1351* operator->() { return &_display; }
    Adafruit_SSD1351& operator*() { return _display; }
    Adafruit_SSD1351* getAdafruitDisplay() { return &_display; }

private:
    Adafruit_SSD1351 _display;
    uint8_t _brightness;
};