/**
 * @file Adxl345.h
 * @brief ADXL345 accelerometer wrapper
 *
 * Provides initialization and data access helpers
 * for the ADXL345 accelerometer.
 */

#pragma once

#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>

#define ADXL_FORCE_SCALE_FACTOR 62.5f
#define ADXL_DURATION_SCALE_FACTOR 0.625f
#define ADXL_LATENCY_SCALE_FACTOR 1.25f

class I2CManager;

/**
 * @brief Adxl345.
 */
class Adxl345 {
public:
    /**
     * @brief Construct ADXL345 wrapper
     */
    Adxl345();

    /**
     * @brief Initialize and configure the ADXL345
     *
     * @param i2c_manager Pointer to initialized I2C manager
     * @return true on success, false otherwise
     */
    bool begin(I2CManager* i2c_manager);

    /**
     * @brief Check if sensor is initialized
     *
     * @return true if ready, false otherwise
     */
    bool isReady() const { return _enabled; }

    /**
     * @brief Read latest sensor event
     *
     * @param event Output event data
     * @return true if event read, false otherwise
     */
    bool getEvent(sensors_event_t* event);

    /**
     * @brief Read a register from the ADXL345
     *
     * @param reg Register address
     * @return Register value
     */
    uint8_t readRegister(uint8_t reg);

    /**
     * @brief Clear pending interrupts
     */
    void clearInterrupts();

    /**
     * @brief Calculate smoothed acceleration magnitude
     *
     * @param accel_x X-axis acceleration in m/s^2
     * @param accel_y Y-axis acceleration in m/s^2
     * @param accel_z Z-axis acceleration in m/s^2
     * @return Smoothed magnitude as integer
     */
    int calculateCombinedMagnitude(float accel_x, float accel_y, float accel_z);

    /**
     * @brief Read FIFO sample count
     *
     * @return Number of samples in FIFO, or 0 if not ready
     */
    uint8_t getFifoSampleCount();

private:
    bool retryInit(I2CManager* i2c_manager, uint8_t attempts);
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t calcGforce(float gforce);
    uint8_t calcDuration(float duration_ms);
    uint8_t calcLatency(float latency_ms);

    Adafruit_ADXL345_Unified _adxl;
    bool _enabled;
};
