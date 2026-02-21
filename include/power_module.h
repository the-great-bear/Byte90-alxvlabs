/**
 * @file power_module.h
 * @brief Header for AXP2101 PMIC power management functionality using XPowersLib
 *
 * Provides functions for initializing and controlling the AXP2101 PMIC,
 * including power rail configuration, battery monitoring, and charging control.
 * Now uses the XPowersLib library for better hardware abstraction.
 */

 #ifndef POWER_MODULE_H
 #define POWER_MODULE_H
 
 #include <Arduino.h>
 #include "i2c_module.h"
 #include <XPowersLib.h>
 
 //==============================================================================
 // CONSTANTS & DEFINITIONS
 //==============================================================================
 
static const char *POWER_LOG = "::POWER_MODULE::";

// Battery Status
 typedef enum {
     BATTERY_STATUS_UNKNOWN = 0,
     BATTERY_STATUS_CHARGING,
     BATTERY_STATUS_DISCHARGING,
     BATTERY_STATUS_FULL,
     BATTERY_STATUS_NOT_PRESENT
 } battery_status_t;
 
// Battery Info Structure
 typedef struct {
     float voltage;              // Battery voltage in volts
     uint8_t percentage;         // Battery percentage (0-100)
     battery_status_t status;    // Battery charging status
     bool present;              // Battery presence
     float temperature;         // Battery temperature (if available)
 } battery_info_t;
 
 //==============================================================================
 // PUBLIC API FUNCTIONS
 //==============================================================================
 
 /**
  * @brief Initialize the AXP2101 power management system using XPowersLib
  * @return true if initialization successful, false otherwise
  */
 bool initializePowerModule(void);
 
 /**
  * @brief Check if AXP2101 is present and responding
  * @return true if AXP2101 is detected, false otherwise
  */
 bool isPowerModuleReady(void);

 /**
  * @brief Read current battery information
  * @param batteryInfo Pointer to structure to store battery info
  * @return true if read successful, false otherwise
  */
 bool getBatteryInfo(battery_info_t *batteryInfo);
 
 /**
  * @brief Read battery voltage with high precision
  * @param voltage Pointer to store voltage value in volts
  * @return true if read successful, false otherwise
  */
 bool readBatteryVoltage(float *voltage);
 
 /**
  * @brief Read battery percentage
  * @param percentage Pointer to store percentage value (0-100)
  * @return true if read successful, false otherwise
  */
 bool readBatteryPercentage(uint8_t *percentage);
 
 /**
  * @brief Display battery status on the OLED screen
  * @param x X position for battery display
  * @param y Y position for battery display
  * @param showDetails Whether to show detailed battery info
  */
 void displayBatteryStatus(int16_t x, int16_t y, bool showDetails = true);
 
 /**
  * @brief Display simple battery status as white text (for debugging)
  * @param x X position for battery display
  * @param y Y position for battery display
  */
 void displaySimpleBatteryText(int16_t x, int16_t y);
 
 /**
  * @brief Display battery icon with percentage overlay
  * @param x X position for battery icon
  * @param y Y position for battery icon
  * @param batteryInfo Battery information structure
  */
 void displayBatteryIcon(int16_t x, int16_t y, const battery_info_t *batteryInfo);
 
 /**
  * @brief Get system power status information
  */
 void printPowerSystemStatus(void);
 
 /**
  * @brief Trigger software shutdown via power button control
  * @return true if shutdown initiated successfully, false otherwise
  */
 bool triggerPowerShutdown(void);
 
 /**
  * @brief Debug function to print power-on and power-off reasons
  */
 void printPowerOnOffStatus(void);

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
 void setPowerModuleDebug(bool enabled);

 //==============================================================================
 // UTILITY FUNCTIONS
 //==============================================================================
 
 /**
  * @brief Convert battery percentage to color for display
  * @param percentage Battery percentage (0-100)
  * @return RGB565 color value
  */
 uint16_t getBatteryColor(uint8_t percentage);
 
 #endif /* POWER_MODULE_H */