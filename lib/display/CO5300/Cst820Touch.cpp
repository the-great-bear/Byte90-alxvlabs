/**
 * Cst820Touch.cpp
 *
 * Implementation for Cst820Touch.
 */

#include "Cst820Touch.h"

CST820_Touch::CST820_Touch()
    : _rst(-1), _irq(-1), _swap_xy(false), _mirror_x(false), _mirror_y(false), _max_x(0), _max_y(0), _chip_id(0),
      _center_btn_x(0), _center_btn_y(0), _home_button_callback(nullptr), _user_data(nullptr), _last_irq_time(0) {}

bool CST820_Touch::begin(int sda, int scl, int rst, int irq) {
    _rst = rst;
    _irq = irq;

    // Hardware reset if RST pin is provided
    if (_rst != -1) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(10);
        digitalWrite(_rst, HIGH);
        delay(50);
    }

    // Setup IRQ pin if provided
    if (_irq != -1) {
        pinMode(_irq, INPUT);
    }

    // Initialize I2C
    Wire.begin(sda, scl);
    delay(50);

    // Read and verify chip ID
    _chip_id = readRegister(CST820_REG_CHIP_ID);

    // Validate chip ID
    if (_chip_id != CST816S_CHIP_ID && _chip_id != CST816T_CHIP_ID && _chip_id != CST816D_CHIP_ID &&
        _chip_id != CST820_CHIP_ID && _chip_id != CST716_CHIP_ID) {
        return false;
    }

    return true;
}

bool CST820_Touch::getTouch(int16_t &x, int16_t &y) {
    // Read touch data (13 bytes starting from register 0x00)
    uint8_t data[13];
    readRegisters(CST820_REG_STATUS, data, 13);

    // Check if touched
    uint8_t touch_num = data[2] & 0x0F;
    if (touch_num == 0 || touch_num == 0xFF) {
        return false;
    }

    // CST820 only supports single touch
    if (touch_num > 1) {
        return false;
    }

    // Extract coordinates
    int16_t tmp_x = ((data[CST820_REG_XPOS_H] & 0x0F) << 8) | data[CST820_REG_XPOS_L];
    int16_t tmp_y = ((data[CST820_REG_YPOS_H] & 0x0F) << 8) | data[CST820_REG_YPOS_L];

    // Check for home button press
    if (_home_button_callback && tmp_x == _center_btn_x && tmp_y == _center_btn_y) {
        _home_button_callback(_user_data);
        return false;
    }

    x = tmp_x;
    y = tmp_y;

    // Apply transformations
    if (_swap_xy) {
        int16_t temp = x;
        x = y;
        y = temp;
    }

    if (_mirror_x && _max_x > 0) {
        x = _max_x - x;
    }

    if (_mirror_y && _max_y > 0) {
        y = _max_y - y;
    }

    return true;
}

bool CST820_Touch::isTouched() {
    // Quick check using IRQ pin if available
    if (_irq != -1) {
        bool is_low = digitalRead(_irq) == LOW;
        if (is_low) {
            // Filter low levels with intervals greater than 1000ms (debounce)
            uint32_t now = millis();
            if (now - _last_irq_time > 1000) {
                return false;
            }
            _last_irq_time = now;
            return true;
        }
        return false;
    }

    // Otherwise read touch number register
    uint8_t touch_num = readRegister(CST820_REG_TOUCH_NUM) & 0x0F;
    return touch_num > 0 && touch_num != 0xFF;
}

uint8_t CST820_Touch::getChipID() {
    return _chip_id;
}

const char *CST820_Touch::getModelName() {
    switch (_chip_id) {
    case CST816S_CHIP_ID:
        return "CST816S";
    case CST816T_CHIP_ID:
        return "CST816T";
    case CST816D_CHIP_ID:
        return "CST816D";
    case CST820_CHIP_ID:
        return "CST820";
    case CST716_CHIP_ID:
        return "CST716";
    default:
        return "UNKNOWN";
    }
}

uint8_t CST820_Touch::getFirmwareVersion() {
    return readRegister(CST820_REG_FW_VERSION);
}

void CST820_Touch::sleep() {
    writeRegister(CST820_REG_SLEEP, 0x03);

    // Set pins to open drain to reduce power consumption
    if (_irq != -1) {
        pinMode(_irq, INPUT);
    }
    if (_rst != -1) {
        pinMode(_rst, INPUT);
    }
}

void CST820_Touch::wakeup() {
    if (_rst != -1) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(10);
        digitalWrite(_rst, HIGH);
        delay(50);
    }
}

void CST820_Touch::disableAutoSleep() {
    // Reset first
    wakeup();
    delay(50);

    // Disable auto-sleep
    writeRegister(CST820_REG_DIS_AUTOSLEEP, 0x01);
}

void CST820_Touch::enableAutoSleep() {
    // Reset first
    wakeup();
    delay(50);

    // Enable auto-sleep
    writeRegister(CST820_REG_DIS_AUTOSLEEP, 0x00);
}

void CST820_Touch::setSwapXY(bool swap) {
    _swap_xy = swap;
}

void CST820_Touch::setMirrorXY(bool mirror_x, bool mirror_y) {
    _mirror_x = mirror_x;
    _mirror_y = mirror_y;
}

void CST820_Touch::setMaxCoordinates(uint16_t max_x, uint16_t max_y) {
    _max_x = max_x;
    _max_y = max_y;
}

void CST820_Touch::setCenterButtonCoordinate(int16_t x, int16_t y) {
    _center_btn_x = x;
    _center_btn_y = y;
}

void CST820_Touch::setHomeButtonCallback(home_button_callback_t callback, void *user_data) {
    _home_button_callback = callback;
    _user_data = user_data;
}

uint8_t CST820_Touch::readRegister(uint8_t reg) {
    Wire.beginTransmission(CST820_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return 0;
    }

    Wire.requestFrom(CST820_I2C_ADDR, 1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0;
}

void CST820_Touch::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(CST820_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

void CST820_Touch::readRegisters(uint8_t reg, uint8_t *buffer, uint8_t len) {
    Wire.beginTransmission(CST820_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return;
    }

    Wire.requestFrom((uint8_t)CST820_I2C_ADDR, (uint8_t)len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) {
        buffer[i] = Wire.read();
    }
}
