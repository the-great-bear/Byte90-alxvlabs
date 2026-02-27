/**
 * Axp2101.cpp
 *
 * Implementation for Axp2101.
 */

#include "Axp2101.h"
#include "I2CManager.h"
#include "DeviceConfig.h"
#include "DeviceSimulator.h"
#include <esp_system.h>
#include <esp_log.h>

static const char* TAG = "AXP2101";

AXP2101::AXP2101(I2CManager* i2c_manager, uint8_t i2c_addr)
    : _i2c_manager(i2c_manager)
    , _addr(i2c_addr)
    , _initialized(false)
    , _debounce_time(100)
    , _last_debounce_time(0)
    , _pending_click(false)
    , _long_press_active(false)
    , _ignore_next_release(false)
    , _on_click(nullptr)
    , _on_long_press(nullptr) {
}

bool AXP2101::begin() {

    if (_initialized) {
        ESP_LOGI(TAG, "Power module already initialized");
        return true;
    }

    // Verify I2C manager is ready
    if (!_i2c_manager || !_i2c_manager->isReady()) {
        ESP_LOGE(TAG, "I2C manager not initialized - cannot initialize AXP2101");
        return false;
    }

    // Get I2C bus from manager
    TwoWire* i2c_bus = _i2c_manager->getBus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "Failed to get I2C bus from manager");
        return false;
    }

    // Verify device is present on the bus
    i2c_bus->beginTransmission(_addr);
    if (i2c_bus->endTransmission() != 0) {
         ESP_LOGE(TAG, "Device not found at address 0x%02X", _addr);
         return false;
    }

    // Initialize AXP2101 using the managed I2C bus
    bool result = _axp.begin(*i2c_bus, _addr, _i2c_manager->getSDAPin(), _i2c_manager->getSCLPin());
    if (!result) {
        ESP_LOGE(TAG, "❌ Failed to initialize AXP2101 - device not responding");
        return false;
    }
    
    ESP_LOGI(TAG, "AXP2101 detected at address 0x%02X", _addr);
    
    // Disable all unused power rails (only DC1 is used on Byte-90)
    _axp.disableDC2();
    _axp.disableDC3();
    _axp.disableDC4();
    _axp.disableDC5();
    _axp.disableALDO1();
    _axp.disableALDO2();
    _axp.disableALDO3();
    _axp.disableALDO4();
    _axp.disableBLDO1();
    _axp.disableBLDO2();
    _axp.disableDLDO1();
    _axp.disableDLDO2();
    
    // Disable low voltage turn off for unused rails
    _axp.disableDC2LowVoltageTurnOff();
    _axp.disableDC3LowVoltageTurnOff();
    _axp.disableDC4LowVoltageTurnOff();
    _axp.disableDC5LowVoltageTurnOff();
    
    ESP_LOGD(TAG, "DC1: %s, %u mV", 
             _axp.isEnableDC1() ? "ON" : "OFF", 
             _axp.getDC1Voltage());
    
    // Power button configuration
    _axp.enablePwrOkPinPullLow();
    _axp.setPowerKeyPressOnTime(XPOWERS_POWERON_512MS);  // 128ms to power on
    _axp.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);   // Long-press threshold
    _axp.disablePwronShutPMIC();                        // Software-controlled shutdown
    _axp.setOnLevel(0);
    
    // Enable only power key interrupts for button detection
    _axp.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    uint64_t irq_mask = XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                        XPOWERS_AXP2101_PKEY_LONG_IRQ |
                        XPOWERS_AXP2101_PKEY_POSITIVE_IRQ;
    bool irq_enabled = _axp.enableIRQ(irq_mask);
    _axp.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S);

    ESP_LOGI(TAG, "IRQs enabled (mask: 0x%llX, enabled: %s)",
             irq_mask, irq_enabled ? "YES" : "NO");

    _axp.clearIrqStatus();

    bool power_on_by_key = _axp.isPwronLowOnSource() || _axp.isIrqLowOnSource();
    esp_reset_reason_t reset_reason = esp_reset_reason();
    bool power_on_reset = (reset_reason == ESP_RST_POWERON);
    if (power_on_by_key && power_on_reset) {
        _ignore_next_release = true;
        ESP_LOGI(TAG, "Power-on by key detected, ignoring first release");
    } else if (power_on_by_key) {
        ESP_LOGI(TAG, "Power-on by key latched, reset reason=%d; not ignoring release",
                 static_cast<int>(reset_reason));
    }
    
    // Setup charging parameters
    setupCharging();
    
    _initialized = true;
    ESP_LOGI(TAG, "AXP2101 initialization complete");
    
    return _initialized;
}

void AXP2101::setupCharging() {
    // Conservative charging parameters for 1200mAh battery
    _axp.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    _axp.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_100MA);
    _axp.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_50MA);
    _axp.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_500MA);
    _axp.setSysPowerDownVoltage(2900);
    _axp.setLowBatShutdownThreshold(5);
    _axp.setThermaThreshold(XPOWERS_AXP2101_THREMAL_80DEG);
    _axp.disableTSPinMeasure();
    _axp.clearIrqStatus();

    ESP_LOGD(TAG, "Charging configured: 500mA constant, 100mA precharge, 50mA termination");
}

void AXP2101::updateButton() {
    if (!_initialized) return;

    // Get IRQ status
    uint64_t irq_status = _axp.getIrqStatus();

    if (irq_status & XPOWERS_AXP2101_PKEY_LONG_IRQ) {
        if (!_long_press_active) {
            _long_press_active = true;
            ESP_LOGI(TAG, "Button long press detected");
            if (_on_long_press) {
                _on_long_press();
            }
        }
    }

    // Check for button release (POSITIVE_IRQ = button released)
    if (irq_status & XPOWERS_AXP2101_PKEY_POSITIVE_IRQ) {
        unsigned long now = millis();

        // Debounce check
        if ((now - _last_debounce_time) >= _debounce_time) {
            _last_debounce_time = now;
            ESP_LOGI(TAG, "Button released");

            if (_ignore_next_release) {
                ESP_LOGI(TAG, "Ignoring release after power-on");
                _ignore_next_release = false;
                _long_press_active = false;
            } else if (_long_press_active) {
                ESP_LOGI(TAG, "Ignoring click due to long press");
                _long_press_active = false;
            } else {
                // Register click on release
                _pending_click = true;
                ESP_LOGI(TAG, "Click event");
            }
        }
    }

    // Clear IRQ status
    if (irq_status) {
        _axp.clearIrqStatus();
    }

    // Process pending events
    processButtonEvents();
}

void AXP2101::processButtonEvents() {
    if (!_pending_click) return;

    // Clear flag first to prevent re-entry
    _pending_click = false;

    if (_on_click) {
        ESP_LOGI(TAG, "Executing click callback");
        _on_click();
    }
}

void AXP2101::onButtonClick(ButtonCallback callback) {
    _on_click = callback;
}

void AXP2101::onButtonLongPress(ButtonCallback callback) {
    _on_long_press = callback;
}

void AXP2101::setDebounceTime(unsigned long ms) {
    _debounce_time = ms;
}

void AXP2101::shutdown() {
    if (!_initialized) {
        return;
    }
    _axp.shutdown();
}

bool AXP2101::getBatteryPercentage(uint8_t* percentage) {
    if (DeviceSimulator::isBatterySimEnabled()) {
        return DeviceSimulator::readBatteryPercentage(percentage);
    }

    if (!_initialized) return false;
    
    // Check if battery is connected
    if (!_axp.isBatteryConnect()) {
        if (percentage) *percentage = 0;
        return false;
    }
    
    if (percentage) {
        *percentage = _axp.getBatteryPercent();
    }
    return true;
}

bool AXP2101::isVbusIn() {
    if (!_initialized) {
        return false;
    }
    return _axp.isVbusIn();
}

bool AXP2101::isCharging() {
    if (!_initialized) {
        return false;
    }
    return _axp.isCharging();
}

void AXP2101::clearIrqStatus() {
    if (!_initialized) {
        return;
    }
    _axp.clearIrqStatus();
}

uint64_t AXP2101::getIrqStatus() {
    if (!_initialized) {
        return 0;
    }
    return _axp.getIrqStatus();
}
