/**
 * @file Adxl345.cpp
 * @brief ADXL345 accelerometer wrapper implementation
 */

#include "Adxl345.h"
#include "DeviceSimulator.h"
#include "I2CManager.h"
#include <esp_log.h>
#include <math.h>

static const char* TAG = "Adxl345";

Adxl345::Adxl345()
    : _adxl(12345)
    , _enabled(false) {
}

bool Adxl345::begin(I2CManager* i2c_manager) {
    if (!retryInit(i2c_manager, 3)) {
        ESP_LOGE(TAG, "❌ ADXL345 initialization failed");
        _enabled = false;
        return false;
    }

    _enabled = true;

    _adxl.setRange(ADXL345_RANGE_16_G);
    _adxl.setDataRate(ADXL345_DATARATE_100_HZ);
    _adxl.writeRegister(ADXL345_REG_INT_ENABLE, 0x00);
    _adxl.writeRegister(ADXL345_REG_THRESH_TAP, calcGforce(14.0f));
    _adxl.writeRegister(ADXL345_REG_DUR, calcDuration(30.0f));
    _adxl.writeRegister(ADXL345_REG_LATENT, calcLatency(100.0f));
    _adxl.writeRegister(ADXL345_REG_WINDOW, calcLatency(250.0f));
    _adxl.writeRegister(ADXL345_REG_TAP_AXES, 0x0F);
    _adxl.writeRegister(ADXL345_REG_INT_MAP, 0x00);
    _adxl.writeRegister(ADXL345_REG_INT_ENABLE, 0x60);
    _adxl.writeRegister(ADXL345_REG_FIFO_CTL, 0x80 | 0x10);
    clearInterrupts();

    ESP_LOGI(TAG, "✅ ADXL345 initialized (range 16G, 100Hz)");

    return true;
}

bool Adxl345::getEvent(sensors_event_t* event) {
    if (!_enabled || !event) {
        return false;
    }

    _adxl.getEvent(event);
    return true;
}

uint8_t Adxl345::readRegister(uint8_t reg) {
    if (!_enabled) {
        ESP_LOGW(TAG, "Read register while sensor disabled");
        return 0;
    }

    return _adxl.readRegister(reg);
}

void Adxl345::clearInterrupts() {
    if (!_enabled) {
        return;
    }

    _adxl.readRegister(ADXL345_REG_INT_SOURCE);
    _adxl.readRegister(ADXL345_REG_INT_SOURCE);
}

int Adxl345::calculateCombinedMagnitude(float accel_x, float accel_y, float accel_z) {
    if (!_enabled) {
        return 0;
    }

    static float smoothed = 0.0f;
    static const float SMOOTHING_FACTOR = 0.1f;

    float raw = sqrtf((accel_x * accel_x) + (accel_y * accel_y) + (accel_z * accel_z));
    float dynamic = fabsf(raw - SENSORS_GRAVITY_EARTH);
    smoothed = (SMOOTHING_FACTOR * dynamic) + ((1.0f - SMOOTHING_FACTOR) * smoothed);

    return (int)roundf(smoothed);
}

uint8_t Adxl345::getFifoSampleCount() {
    if (!_enabled) {
        return 0;
    }

    uint8_t status = _adxl.readRegister(ADXL345_REG_FIFO_STATUS);
    return status & 0x3F;
}

bool Adxl345::retryInit(I2CManager* i2c_manager, uint8_t attempts) {
    if (!i2c_manager || !i2c_manager->isReady()) {
        ESP_LOGE(TAG, "❌ I2C manager not initialized");
        return false;
    }

    TwoWire* bus = i2c_manager->getBus();
    if (!bus) {
        ESP_LOGE(TAG, "❌ I2C bus not available");
        return false;
    }

    uint8_t retries_left = attempts;
    while (retries_left--) {
        bus->beginTransmission(ADXL345_DEFAULT_ADDRESS);
        byte error = bus->endTransmission();
        if (error != 0) {
            ESP_LOGW(TAG, "I2C error %d, retries left: %d", error, retries_left);
            if (retries_left > 0) {
                delay(500);
                continue;
            }
            return false;
        }

        if (_adxl.begin(ADXL345_DEFAULT_ADDRESS)) {
            sensors_event_t event;
            _adxl.getEvent(&event);
            _adxl.readRegister(ADXL345_REG_INT_SOURCE);
            return true;
        }

        ESP_LOGW(TAG, "ADXL345 begin failed, retries left: %d", retries_left);
        delay(500);
    }

    return false;
}

void Adxl345::writeRegister(uint8_t reg, uint8_t value) {
    if (!_enabled) {
        return;
    }

    _adxl.writeRegister(reg, value);
}

uint8_t Adxl345::calcGforce(float gforce) {
    return min((uint8_t)(gforce * 1000.0f / ADXL_FORCE_SCALE_FACTOR), (uint8_t)255);
}

uint8_t Adxl345::calcDuration(float duration_ms) {
    return min((uint8_t)(duration_ms / ADXL_DURATION_SCALE_FACTOR), (uint8_t)255);
}

uint8_t Adxl345::calcLatency(float latency_ms) {
    return min((uint8_t)(latency_ms / ADXL_LATENCY_SCALE_FACTOR), (uint8_t)255);
}
