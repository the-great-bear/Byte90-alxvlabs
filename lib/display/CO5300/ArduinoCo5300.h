#pragma once

/*
 * Consolidated CO5300 AMOLED Display Driver
 * Combines TFT optimizations + CO5300 controller logic
 * Works with Adafruit_GFX library
 */

#include "ArduinoEsp32Qspi.h"
#include <Adafruit_GFX.h>

// Timing delays
#define CO5300_RST_DELAY 200
#define CO5300_SLPIN_DELAY 120
#define CO5300_SLPOUT_DELAY 120

// Display configuration
#define CO5300_DEFAULT_WIDTH 460
#define CO5300_DEFAULT_HEIGHT 460
#define CO5300_DEFAULT_ROTATION 0
#define CO5300_DEFAULT_RST -1
#define CO5300_DEFAULT_IPS false
#define CO5300_DEFAULT_COL_OFFSET1 6
#define CO5300_DEFAULT_ROW_OFFSET1 0
#define CO5300_DEFAULT_COL_OFFSET2 0
#define CO5300_DEFAULT_ROW_OFFSET2 0

// CO5300 Commands
#define CO5300_C_NOP 0x00
#define CO5300_C_SWRESET 0x01
#define CO5300_C_SLPIN 0x10
#define CO5300_C_SLPOUT 0x11
#define CO5300_C_INVOFF 0x20
#define CO5300_C_INVON 0x21
#define CO5300_C_DISPOFF 0x28
#define CO5300_C_DISPON 0x29
#define CO5300_W_CASET 0x2A
#define CO5300_W_PASET 0x2B
#define CO5300_W_RAMWR 0x2C
#define CO5300_WC_TEARON 0x35
#define CO5300_W_MADCTL 0x36
#define CO5300_W_PIXFMT 0x3A
#define CO5300_W_WDBRIGHTNESSVALNOR 0x51
#define CO5300_W_WCTRLD1 0x53
#define CO5300_W_WCE 0x58
#define CO5300_W_WDBRIGHTNESSVALHBM 0x63
#define CO5300_W_SPIMODECTL 0xC4

// MADCTL bits
#define CO5300_MADCTL_RGB 0x00
#define CO5300_MADCTL_BGR 0x08
#define CO5300_MADCTL_COLOR_ORDER CO5300_MADCTL_RGB
#define CO5300_MADCTL_X_AXIS_FLIP 0x02
#define CO5300_MADCTL_Y_AXIS_FLIP 0x05

// Contrast modes
enum { CO5300_CONTRAST_OFF = 0, CO5300_CONTRAST_LOW, CO5300_CONTRAST_MEDIUM, CO5300_CONTRAST_HIGH };

/**
 * @brief Arduino_CO5300.
 */
class Arduino_CO5300 : public Adafruit_GFX {
public:
    // Factory method for easy initialization
    static Arduino_CO5300 *createDisplay(int8_t cs, int8_t sck, int8_t sdio0, int8_t sdio1, int8_t sdio2, int8_t sdio3,
                                         int8_t rst = CO5300_DEFAULT_RST, int16_t width = CO5300_DEFAULT_WIDTH,
                                         int16_t height = CO5300_DEFAULT_HEIGHT,
                                         uint8_t rotation = CO5300_DEFAULT_ROTATION);

    Arduino_CO5300(Arduino_ESP32QSPI *bus, int8_t rst = -1, uint8_t rotation = 0, bool ips = false,
                   int16_t w = CO5300_DEFAULT_WIDTH, int16_t h = CO5300_DEFAULT_HEIGHT, uint8_t col_offset1 = 0,
                   uint8_t row_offset1 = 0, uint8_t col_offset2 = 0, uint8_t row_offset2 = 0);

    // Initialization
    /**
     * @brief Initialize and start the component
     *
     * @param speed Speed
     *
     * @return true on success, false on failure
     */
    bool begin(int32_t speed = 80000000);

    // Adafruit_GFX required overrides
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void setRotation(uint8_t r) override;

    // Display control
    /**
     * @brief Invert display
     *
     * @param i true if i, false otherwise
     */
    void invertDisplay(bool i);
    /**
     * @brief Display on
     */
    void displayOn();
    /**
     * @brief Display off
     */
    void displayOff();
    /**
     * @brief Set the brightness
     *
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);
    /**
     * @brief Set the contrast
     *
     * @param contrast Numeric value
     */
    void setContrast(uint8_t contrast);

    // Optimized drawing methods
    /**
     * @brief Start write
     */
    void startWrite();
    /**
     * @brief End write
     */
    void endWrite();
    /**
     * @brief Write data
     *
     * @param x Numeric value
     * @param y Numeric value
     * @param color Numeric value
     */
    void writePixel(int16_t x, int16_t y, uint16_t color);
    /**
     * @brief Write data
     *
     * @param x Numeric value
     * @param y Numeric value
     * @param w Numeric value
     * @param h Numeric value
     * @param color Numeric value
     */
    void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

protected:
    // Display initialization
    /**
     * @brief Tft init
     */
    void tftInit();

    // Address window management
    /**
     * @brief Write data
     *
     * @param x Numeric value
     * @param y Numeric value
     * @param w Numeric value
     * @param h Numeric value
     */
    void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h);
    /**
     * @brief Write data
     *
     * @param color Numeric value
     * @param len Number of elements
     */
    void writeRepeat(uint16_t color, uint32_t len);
    /**
     * @brief Write data
     *
     * @param color Numeric value
     */
    void writeColor(uint16_t color);

    // Hardware
    Arduino_ESP32QSPI *_bus;
    int8_t _rst;
    bool _ips;

    // Display offsets
    uint8_t _col_offset1, _row_offset1;
    uint8_t _col_offset2, _row_offset2;
    uint8_t _x_start, _y_start;

    // Address window caching
    int16_t _current_x, _current_y;
    uint16_t _current_w, _current_h;

    // Display dimensions
    int16_t _max_x, _max_y;
};
