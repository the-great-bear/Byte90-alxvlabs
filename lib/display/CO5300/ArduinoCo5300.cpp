/*
 * Consolidated CO5300 AMOLED Display Driver
 * Combines TFT optimizations + CO5300 controller logic
 */

#include "ArduinoCo5300.h"

// Factory method for easy initialization
Arduino_CO5300 *Arduino_CO5300::createDisplay(int8_t cs, int8_t sck, int8_t sdio0, int8_t sdio1, int8_t sdio2,
                                              int8_t sdio3, int8_t rst, int16_t width, int16_t height,
                                              uint8_t rotation) {
    Arduino_ESP32QSPI *bus = new Arduino_ESP32QSPI(cs, sck, sdio0, sdio1, sdio2, sdio3);
    return new Arduino_CO5300(bus, rst, rotation, CO5300_DEFAULT_IPS, width, height, CO5300_DEFAULT_COL_OFFSET1,
                              CO5300_DEFAULT_ROW_OFFSET1, CO5300_DEFAULT_COL_OFFSET2, CO5300_DEFAULT_ROW_OFFSET2);
}

// Initialization sequence for CO5300
static const uint8_t co5300_init_operations[] = {
    0x01,                      // BEGIN_WRITE
    0x02, CO5300_C_SLPOUT,     // WRITE_COMMAND_8, Sleep Out
    0x0B,                      // END_WRITE
    0x0C, CO5300_SLPOUT_DELAY, // DELAY

    0x01, // BEGIN_WRITE
    0x07, 0xFE,
    0x00, // WRITE_C8_D8, Page select
    0x07, CO5300_W_SPIMODECTL,
    0x80, // WRITE_C8_D8, SPI mode
    0x07, CO5300_W_PIXFMT,
    0x55, // WRITE_C8_D8, RGB565
    0x07, CO5300_W_WCTRLD1,
    0x20, // WRITE_C8_D8, Display control
    0x07, CO5300_W_WDBRIGHTNESSVALHBM,
    0xFF,                  // WRITE_C8_D8, HBM brightness
    0x02, CO5300_C_DISPON, // WRITE_COMMAND_8, Display ON
    0x07, CO5300_W_WDBRIGHTNESSVALNOR,
    0xD0, // WRITE_C8_D8, Normal brightness
    0x07, CO5300_W_WCE,
    0x00,    // WRITE_C8_D8, Contrast off
    0x0B,    // END_WRITE
    0x0C, 10 // DELAY
};

Arduino_CO5300::Arduino_CO5300(Arduino_ESP32QSPI *bus, int8_t rst, uint8_t rotation, bool ips, int16_t w, int16_t h,
                               uint8_t col_offset1, uint8_t row_offset1, uint8_t col_offset2, uint8_t row_offset2)
    : Adafruit_GFX(w, h), _bus(bus), _rst(rst), _ips(ips), _col_offset1(col_offset1), _row_offset1(row_offset1),
      _col_offset2(col_offset2), _row_offset2(row_offset2) {
    _max_x = w - 1;
    _max_y = h - 1;
    _current_x = 0xFFFF;
    _current_y = 0xFFFF;
    _current_w = 0xFFFF;
    _current_h = 0xFFFF;

    // Call parent setRotation to set internal state, but don't write to hardware yet
    Adafruit_GFX::setRotation(rotation);

    // Set offsets based on rotation without writing to bus
    switch (rotation) {
    case 1:
        _x_start = _row_offset1;
        _y_start = _col_offset2;
        break;
    case 2:
        _x_start = _col_offset2;
        _y_start = _row_offset2;
        break;
    case 3:
        _x_start = _row_offset2;
        _y_start = _col_offset1;
        break;
    default: // case 0:
        _x_start = _col_offset1;
        _y_start = _row_offset1;
        break;
    }
}

bool Arduino_CO5300::begin(int32_t speed) {
    if (!_bus->begin(speed)) {
        return false;
    }

    tftInit();
    setRotation(getRotation());

    return true;
}

void Arduino_CO5300::tftInit() {
    if (_rst != -1) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, HIGH);
        delay(10);
        digitalWrite(_rst, LOW);
        delay(CO5300_RST_DELAY);
        digitalWrite(_rst, HIGH);
        delay(CO5300_RST_DELAY);
    } else {
        _bus->sendCommand(CO5300_C_SWRESET);
        delay(CO5300_RST_DELAY);
    }

    _bus->batchOperation(co5300_init_operations, sizeof(co5300_init_operations));
}

void Arduino_CO5300::setRotation(uint8_t r) {
    Adafruit_GFX::setRotation(r);

    uint8_t madctl;
    switch (rotation) {
    case 1:
        madctl = CO5300_MADCTL_COLOR_ORDER | CO5300_MADCTL_X_AXIS_FLIP;
        _x_start = _row_offset1;
        _y_start = _col_offset2;
        break;
    case 2:
        madctl = CO5300_MADCTL_COLOR_ORDER | CO5300_MADCTL_X_AXIS_FLIP | CO5300_MADCTL_Y_AXIS_FLIP;
        _x_start = _col_offset2;
        _y_start = _row_offset2;
        break;
    case 3:
        madctl = CO5300_MADCTL_COLOR_ORDER | CO5300_MADCTL_Y_AXIS_FLIP;
        _x_start = _row_offset2;
        _y_start = _col_offset1;
        break;
    default: // case 0:
        madctl = CO5300_MADCTL_COLOR_ORDER;
        _x_start = _col_offset1;
        _y_start = _row_offset1;
        break;
    }

    _bus->beginWrite();
    _bus->writeC8D8(CO5300_W_MADCTL, madctl);
    _bus->endWrite();

    // Invalidate address window cache
    _current_x = 0xFFFF;
    _current_y = 0xFFFF;
    _current_w = 0xFFFF;
    _current_h = 0xFFFF;
}

void Arduino_CO5300::writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if ((x != _current_x) || (w != _current_w) || (y != _current_y) || (h != _current_h)) {
        _current_x = x;
        _current_y = y;
        _current_w = w;
        _current_h = h;

        x += _x_start;
        _bus->writeC8D16D16(CO5300_W_CASET, x, x + w - 1);
        y += _y_start;
        _bus->writeC8D16D16(CO5300_W_PASET, y, y + h - 1);
    }

    _bus->writeCommand(CO5300_W_RAMWR);
}

void Arduino_CO5300::writeRepeat(uint16_t color, uint32_t len) {
    _bus->writeRepeat(color, len);
}

void Arduino_CO5300::writeColor(uint16_t color) {
    _bus->write16(color);
}

void Arduino_CO5300::startWrite() {
    _bus->beginWrite();
}

void Arduino_CO5300::endWrite() {
    _bus->endWrite();
}

void Arduino_CO5300::writePixel(int16_t x, int16_t y, uint16_t color) {
    if ((x >= 0) && (x <= _max_x) && (y >= 0) && (y <= _max_y)) {
        writeAddrWindow(x, y, 1, 1);
        writeColor(color);
    }
}

void Arduino_CO5300::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x >= 0) && (x < width()) && (y >= 0) && (y < height())) {
        startWrite();
        writePixel(x, y, color);
        endWrite();
    }
}

void Arduino_CO5300::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w && h) {
        if (w < 0) {
            x += w + 1;
            w = -w;
        }
        if (x <= _max_x) {
            if (h < 0) {
                y += h + 1;
                h = -h;
            }
            if (y <= _max_y) {
                int16_t x2 = x + w - 1;
                if (x2 >= 0) {
                    int16_t y2 = y + h - 1;
                    if (y2 >= 0) {
                        if (x < 0) {
                            x = 0;
                            w = x2 + 1;
                        }
                        if (y < 0) {
                            y = 0;
                            h = y2 + 1;
                        }
                        if (x2 > _max_x) {
                            w = _max_x - x + 1;
                        }
                        if (y2 > _max_y) {
                            h = _max_y - y + 1;
                        }

                        writeAddrWindow(x, y, w, h);
                        writeRepeat(color, (uint32_t)w * h);
                    }
                }
            }
        }
    }
}

void Arduino_CO5300::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    startWrite();
    writeFillRect(x, y, w, h, color);
    endWrite();
}

void Arduino_CO5300::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void Arduino_CO5300::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void Arduino_CO5300::invertDisplay(bool i) {
    _bus->sendCommand((_ips ^ i) ? CO5300_C_INVON : CO5300_C_INVOFF);
}

void Arduino_CO5300::displayOn() {
    _bus->sendCommand(CO5300_C_DISPON);
    delay(CO5300_SLPIN_DELAY);
    _bus->sendCommand(CO5300_C_SLPOUT);
    delay(CO5300_SLPOUT_DELAY);
}

void Arduino_CO5300::displayOff() {
    _bus->sendCommand(CO5300_C_DISPOFF);
    delay(CO5300_SLPIN_DELAY);
    _bus->sendCommand(CO5300_C_SLPIN);
    delay(CO5300_SLPIN_DELAY);
}

void Arduino_CO5300::setBrightness(uint8_t brightness) {
    _bus->beginWrite();
    _bus->writeC8D8(CO5300_W_WDBRIGHTNESSVALNOR, brightness);
    _bus->endWrite();
}

void Arduino_CO5300::setContrast(uint8_t contrast) {
    uint8_t value;
    switch (contrast) {
    case CO5300_CONTRAST_LOW:
        value = 0x05;
        break;
    case CO5300_CONTRAST_MEDIUM:
        value = 0x06;
        break;
    case CO5300_CONTRAST_HIGH:
        value = 0x07;
        break;
    default: // CO5300_CONTRAST_OFF
        value = 0x00;
        break;
    }

    _bus->beginWrite();
    _bus->writeC8D8(CO5300_W_WCE, value);
    _bus->endWrite();
}
