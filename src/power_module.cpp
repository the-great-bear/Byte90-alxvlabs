/**
 * @file power_module.cpp
 * @brief Implementation of AXP2101 PMIC power management using XPowersLib
 *
 * Provides functions for initializing and controlling the AXP2101 PMIC,
 * including power rail configuration, battery monitoring, and charging control.
 * Updated to use XPowersLib for better hardware abstraction and reliability.
 */

#include "power_module.h"
#include "common.h"
#include "display_module.h"
#include "esp_log.h"
#include <stdarg.h>

//==============================================================================
// DEBUG SYSTEM
//==============================================================================

// Debug control - set to true to enable debug logging
static bool powerModuleDebugEnabled = false;

/**
 * @brief Centralized debug logging function for power module operations
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
static void powerModuleDebug(const char* format, ...) {
  if (!powerModuleDebugEnabled) {
    return;
  }
  
  va_list args;
  va_start(args, format);
  esp_log_writev(ESP_LOG_INFO, "POWER_MODULE", format, args);
  va_end(args);
}

/**
 * @brief Enable or disable debug logging for power module operations
 * @param enabled true to enable debug logging, false to disable
 * 
 * @example
 * // Enable debug logging
 * setPowerModuleDebug(true);
 * 
 * // Disable debug logging  
 * setPowerModuleDebug(false);
 */
void setPowerModuleDebug(bool enabled) {
  powerModuleDebugEnabled = enabled;
  if (enabled) {
    ESP_LOGI("POWER_MODULE", "Power module debug logging enabled");
  } else {
    ESP_LOGI("POWER_MODULE", "Power module debug logging disabled");
  }
}

//==============================================================================
// GLOBAL VARIABLES
//==============================================================================

static XPowersAXP2101 power; // XPowersLib AXP2101 instance
static bool powerModuleInitialized = false;

//==============================================================================
// UTILITY FUNCTIONS
//==============================================================================

/**
 * @brief Convert battery percentage to color for display
 * @param percentage Battery percentage (0-100)
 * @return RGB565 color value
 */
uint16_t getBatteryColor(uint8_t percentage) {
  if (percentage > 50) {
    return 0x07E0; // Green (good battery)
  } else if (percentage > 20) {
    return COLOR_YELLOW; // Yellow (medium battery)
  } else {
    return 0xF800; // Red (low battery)
  }
}

//==============================================================================
// PUBLIC API FUNCTIONS
//==============================================================================

void setupSafeCharging() {
  // CHANGE: Set constant charge current to 500mA (default is EFUSE, likely
  // higher) For 1200mAh battery: 500mA = 0.42C (conservative and safe)
  power.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
  // Precharge current default is 125mA (~10% of capacity) - acceptable, but
  // reduce to 100mA
  power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_100MA);
  // CHANGE: Termination current to 50mA (default is 125mA, which is too high)
  power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_50MA);
  // CHANGE: Reduce VBUS input current limit to 500mA (default is 1.5A)
  // Use 500mA for USB 2.0 compatibility
  power.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_500MA);
  // CHANGE: Set system power down voltage to 2.9V (default is EFUSE,
  // likely 3.7V) 2.9V is safer minimum for Li-ion
  power.setSysPowerDownVoltage(2900);
  // CHANGE: Low battery shutdown from 1% to 5% for better protection
  power.setLowBatShutdownThreshold(5);
  power.setThermaThreshold(XPOWERS_AXP2101_THREMAL_80DEG);
  // DISABLE TS pin measurement (enabled by default, but likely no thermistor
  // connected) This will set TS pin to external fixed input mode
  power.disableTSPinMeasure();
  // Clear any pending interrupts from power-on
  power.clearIrqStatus();
}

/**
 * @brief Initialize the AXP2101 power management system using XPowersLib
 * @return true if initialization successful, false otherwise
 */
bool initializePowerModule(void) {
  ESP_LOGE(POWER_LOG, "=== initializePowerModule() called ===");
  ESP_LOGE(POWER_LOG, "AXP2101 compile flag: %s", AXP2101 ? "ENABLED" : "DISABLED");
  
  if (!checkAXPSupport()) {
    ESP_LOGE(POWER_LOG, "AXP2101 support disabled at compile time - skipping initialization");
    powerModuleInitialized = false;
    return false;
  }

  if (powerModuleInitialized) {
    powerModuleDebug("Power module already initialized");
    return true;
  }

  powerModuleDebug("=== Starting Power Module Initialization (XPowersLib) ===");
  // Ensure I2C is initialized
  if (!isI2CReady()) {
    ESP_LOGE(POWER_LOG, "❌ I2C bus not ready - cannot initialize power module");
    return false;
  }
  powerModuleDebug("✓ I2C bus is ready");

  // Initialize XPowersLib with the I2C bus
  TwoWire *i2cBus = getI2CBus();
  if (!i2cBus) {
    ESP_LOGE(POWER_LOG, "❌ Failed to get I2C bus instance");
    return false;
  }

  // Begin communication with AXP2101
  bool result =
      power.begin(*i2cBus, AXP2101_SLAVE_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
  if (!result) {
    ESP_LOGE(POWER_LOG, "❌ Failed to initialize AXP2101 - device not responding");
    return false;
  }

  // IMPORTANT: Since only DC1 is enabled in hardware, must disable all other
  // rails.
  power.disableDC2();
  power.disableDC3();
  power.disableDC4();
  power.disableDC5();
  power.disableALDO1();
  power.disableALDO2();
  power.disableALDO3();
  power.disableALDO4();
  power.disableBLDO1();
  power.disableBLDO2();
  power.disableDLDO1();
  power.disableDLDO2();

  // NOTE: Important to disable low voltage turn off for all rails
  // otherwise PMU will not power on
  power.disableDC2LowVoltageTurnOff();
  power.disableDC3LowVoltageTurnOff();
  power.disableDC4LowVoltageTurnOff();
  power.disableDC5LowVoltageTurnOff();

  powerModuleDebug("✓ DC1: %s, %u mV", power.isEnableDC1() ? "ON" : "OFF", power.getDC1Voltage());

  power.setPowerKeyPressOnTime(XPOWERS_POWERON_2S); 
  power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);

  setupSafeCharging();

  powerModuleInitialized = true;
  powerModuleDebug("=== Power Module Initialization Complete ===");
  return true;
}

/**
 * @brief Check if AXP2101 is present and responding
 * @return true if AXP2101 is detected, false otherwise
 */
bool isPowerModuleReady(void) { return powerModuleInitialized; }

/**
 * @brief Read current battery information
 * @param batteryInfo Pointer to structure to store battery info
 * @return true if read successful, false otherwise
 */
bool getBatteryInfo(battery_info_t *batteryInfo) {
  if (!powerModuleInitialized || !batteryInfo) {
    return false;
  }

  // Check if battery is connected
  batteryInfo->present = power.isBatteryConnect();

  if (!batteryInfo->present) {
    batteryInfo->voltage = 0.0f;
    batteryInfo->percentage = 0;
    batteryInfo->status = BATTERY_STATUS_NOT_PRESENT;
    batteryInfo->temperature = 0.0f;
    return true;
  }

  // Read battery voltage (convert from mV to V)
  batteryInfo->voltage = power.getBattVoltage() / 1000.0f;

  // Read battery percentage
  batteryInfo->percentage = power.getBatteryPercent();

  // Determine charging status
  if (power.isCharging()) {
    uint8_t chargeStatus = power.getChargerStatus();
    if (chargeStatus == XPOWERS_AXP2101_CHG_DONE_STATE) {
      batteryInfo->status = BATTERY_STATUS_FULL;
    } else {
      batteryInfo->status = BATTERY_STATUS_CHARGING;
    }
  } else if (power.isDischarge()) {
    batteryInfo->status = BATTERY_STATUS_DISCHARGING;
  } else {
    batteryInfo->status = BATTERY_STATUS_UNKNOWN;
  }

  // Temperature not available on this hardware
  batteryInfo->temperature = 0.0f;

  return true;
}

/**
 * @brief Read battery voltage with high precision
 * @param voltage Pointer to store voltage value in volts
 * @return true if read successful, false otherwise
 */
bool readBatteryVoltage(float *voltage) {
  if (!powerModuleInitialized || !voltage) {
    return false;
  }

  if (!power.isBatteryConnect()) {
    *voltage = 0.0f;
    return false;
  }

  // Convert from millivolts to volts
  *voltage = power.getBattVoltage() / 1000.0f;
  return true;
}

/**
 * @brief Read battery percentage
 * @param percentage Pointer to store percentage value (0-100)
 * @return true if read successful, false otherwise
 */
bool readBatteryPercentage(uint8_t *percentage) {
  if (!powerModuleInitialized || !percentage) {
    return false;
  }

  if (!power.isBatteryConnect()) {
    *percentage = 0;
    return false;
  }

  *percentage = power.getBatteryPercent();
  return true;
}

/**
 * @brief Get system power status information
 */
void printPowerSystemStatus(void) {
  if (!powerModuleInitialized) {
    ESP_LOGW(POWER_LOG, "Power module not initialized");
    return;
  }

  powerModuleDebug("=== Power System Status ===");
  powerModuleDebug("Charging: %s", power.isCharging() ? "YES" : "NO");
  powerModuleDebug("Discharging: %s", power.isDischarge() ? "YES" : "NO");
  powerModuleDebug("VBUS In: %s", power.isVbusIn() ? "YES" : "NO");
  powerModuleDebug("Battery Voltage: %u mV", power.getBattVoltage());
  powerModuleDebug("VBUS Voltage: %u mV", power.getVbusVoltage());
  powerModuleDebug("System Voltage: %u mV", power.getSystemVoltage());

  if (power.isBatteryConnect()) {
    powerModuleDebug("Battery Percentage: %u%%", power.getBatteryPercent());

    uint8_t chargeStatus = power.getChargerStatus();
    const char *statusStr = "Unknown";
    switch (chargeStatus) {
    case XPOWERS_AXP2101_CHG_TRI_STATE:
      statusStr = "Trickle Charge";
      break;
    case XPOWERS_AXP2101_CHG_PRE_STATE:
      statusStr = "Pre-charge";
      break;
    case XPOWERS_AXP2101_CHG_CC_STATE:
      statusStr = "Constant Current";
      break;
    case XPOWERS_AXP2101_CHG_CV_STATE:
      statusStr = "Constant Voltage";
      break;
    case XPOWERS_AXP2101_CHG_DONE_STATE:
      statusStr = "Charge Complete";
      break;
    case XPOWERS_AXP2101_CHG_STOP_STATE:
      statusStr = "Not Charging";
      break;
    }
    powerModuleDebug("Charge Status: %s", statusStr);
  } else {
    powerModuleDebug("Battery: Not connected");
  }

  powerModuleDebug("=========================");
}

/**
 * @brief Debug function to print power-on and power-off reasons
 */
void printPowerOnOffStatus(void) {
  if (!powerModuleInitialized) {
    return;
  }

  powerModuleDebug("=== Power On/Off Status ===");
  powerModuleDebug("VBUS Good: %s", power.isVbusGood() ? "YES" : "NO");
  powerModuleDebug("Battery Connected: %s",
           power.isBatteryConnect() ? "YES" : "NO");
  powerModuleDebug("===========================");
}

/**
 * @brief Display battery status on the OLED screen
 * @param x X position for battery display
 * @param y Y position for battery display
 * @param showDetails Whether to show detailed battery info
 */
void displayBatteryStatus(int16_t x, int16_t y, bool showDetails) {
  if (!powerModuleInitialized) {
    return;
  }

  battery_info_t batteryInfo;
  if (!getBatteryInfo(&batteryInfo)) {
    return;
  }

  displayBatteryIcon(x, y, &batteryInfo);

  if (showDetails) {
    // Display voltage and percentage below icon
    // Note: Would need display module functions here
  }
}

/**
 * @brief Display simple battery status as white text (for debugging)
 * @param x X position for battery display
 * @param y Y position for battery display
 */
void displaySimpleBatteryText(int16_t x, int16_t y) {
  if (!powerModuleInitialized) {
    return;
  }

  battery_info_t batteryInfo;
  if (!getBatteryInfo(&batteryInfo)) {
    return;
  }

  // Note: Would need display module functions here
}

/**
 * @brief Display battery icon with percentage overlay
 * @param x X position for battery icon
 * @param y Y position for battery icon
 * @param batteryInfo Battery information structure
 */
void displayBatteryIcon(int16_t x, int16_t y,
                        const battery_info_t *batteryInfo) {
  if (!batteryInfo) {
    return;
  }

  // Note: Would need display module functions to actually draw
  // This is just a placeholder showing the logic
  // Draw battery outline, fill based on percentage, etc.
  // Implementation depends on display_module.h functions
}

/**
 * @brief Trigger software shutdown via power button control
 * @return true if shutdown initiated successfully, false otherwise
 */
bool triggerPowerShutdown() {
  if (!powerModuleInitialized) {
    ESP_LOGE(POWER_LOG, "Power module not initialized");
    return false;
  }

  powerModuleDebug("Initiating software power shutdown...");

  // Use XPowersLib's shutdown function
  power.shutdown();

  return true;
}