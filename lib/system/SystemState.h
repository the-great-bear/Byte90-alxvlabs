/**
 * SystemState.h
 *
 * Declarations for SystemState.
 */

#pragma once

// System includes
#include <Arduino.h>
#include <functional>
#include <vector>

// Forward declarations
/**
 * @brief WifiManager.
 */
class WifiManager;
/**
 * @brief NVSStorage.
 */
class NVSStorage;
/**
 * @brief LanguageManager.
 */
class LanguageManager;

/**
 * @brief input.
 * SystemState - Core system states for the Xiaozhi device
 *
 * State machine that controls device behavior and UI feedback.
 * Based on Xiaozhi ESP-IDF implementation.
 */
enum SystemState {
  SYSTEM_STATE_UNKNOWN = 0,          // Initial/unknown state
  SYSTEM_STATE_STARTING = 1,         // Device booting up
  SYSTEM_STATE_WIFI_CONFIGURING = 2, // WiFi setup mode
  SYSTEM_STATE_IDLE = 3,             // Standby, waiting for input (DEFAULT)
  SYSTEM_STATE_CONNECTING = 4,       // Connecting to server
  SYSTEM_STATE_LISTENING = 5,        // Recording audio
  SYSTEM_STATE_SPEAKING = 6,         // Playing TTS audio
  SYSTEM_STATE_LOADING = 7,          // Fetching remote data
  SYSTEM_STATE_ACTIVATING = 8,       // Device activation
};

/**
 * @brief Convert SystemState enum to human-readable string
 *
 * @param state System state enum value
 * @return String representation of state
 */
inline const char *SystemStateToString(SystemState state) {
  switch (state) {
  case SYSTEM_STATE_UNKNOWN:
    return "Unknown";
  case SYSTEM_STATE_STARTING:
    return "Starting";
  case SYSTEM_STATE_WIFI_CONFIGURING:
    return "WiFi Config";
  case SYSTEM_STATE_IDLE:
    return "Idle";
  case SYSTEM_STATE_CONNECTING:
    return "Connecting";
  case SYSTEM_STATE_LISTENING:
    return "Listening";
  case SYSTEM_STATE_SPEAKING:
    return "Speaking";
  case SYSTEM_STATE_LOADING:
    return "Loading";
  case SYSTEM_STATE_ACTIVATING:
    return "Activating";
  default:
    return "Invalid State";
  }
}

// Convenience constant for common state
const SystemState IDLE_MODE = SYSTEM_STATE_IDLE;

/**
 * SystemStateManager - System state machine manager
 *
 * Features:
 * - State transitions with validation
 * - State change callbacks
 * - State history tracking
 *
 * Architecture:
 * - Single global state machine
 * - Event-driven state changes
 * - Observer pattern for state notifications
 */
class SystemStateManager {
public:
  /**
   * @brief Construct system state manager instance
   */
  SystemStateManager();

  /**
   * @brief Initialize and start the component
   * @param wifi WiFi manager for monitoring connection status
   * @param lang Language manager for status display
   */
  bool begin(WifiManager *wifi = nullptr, LanguageManager *lang = nullptr);

  /**
   * @brief Process periodic tasks and events
   */
  void loop();

  /**
   * @brief Set status logging interval
   * @param interval_ms Interval in milliseconds (default: 5000ms)
   */
  void setStatusLogInterval(unsigned long interval_ms);

  /**
   * @brief Initialize WiFi and set appropriate state based on credentials
   * @param nvs NVS storage for checking saved credentials
   * @return true if WiFi initialized, false otherwise
   */
  bool initializeWiFi(NVSStorage *nvs);

  /**
   * @brief Setup WiFi event callbacks for connection logging
   */
  void setupWiFiCallbacks();

  // System state management

  /**
   * @brief Get the current system state
   *
   * @return Current state
   */
  SystemState getState() { return _state; }

  /**
   * @brief Get device UUID
   */
  const char *getDeviceUUID() const { return _deviceUUID.c_str(); }

  /**
   * @brief Set the system state
   *
   * @param state New state to transition to
   */
  void setState(SystemState state);

  /**
   * @brief Get string representation of a state
   *
   * @param state System state enum value
   * @return String representation
   */
  const char *getStateString(SystemState state);

  /**
   * @brief Print comprehensive system status including state, WiFi, and memory
   * @param wifi_connected WiFi connection status
   * @param wifi_ssid WiFi network name (if connected)
   * @param ap_clients Number of AP clients connected
   */
  void printSystemStatus(bool wifi_connected = false,
                         const char *wifi_ssid = "", int ap_clients = 0);

  // State change event callbacks

  /**
   * @brief Register callback for state change events
   *
   * @param callback Function to call when state changes (previous_state,
   * current_state)
   */
  void registerStateChangeCallback(
      std::function<void(SystemState, SystemState)> callback);

private:
  // State tracking
  SystemState _state;
  SystemState _previousState;
  // Callbacks
  std::vector<std::function<void(SystemState, SystemState)>>
      _stateChangeCallbacks;
  // WiFi monitoring
  WifiManager *_wifi;
  bool _lastWifiState;
  // Language manager for status display
  LanguageManager *_lang;

  // Status logging
  unsigned long _lastStatusLog;
  unsigned long _statusLogInterval;

  // Device identification
  String _deviceUUID;
  /**
   * @brief Post state change event to all registered callbacks
   *
   * @param previous_state Previous state
   * @param current_state Current state
   */
  void postStateChangeEvent(SystemState previous_state,
                            SystemState current_state);
  /**
   * @brief Monitor WiFi connection and handle state transitions
   */
  void updateWiFiState();

  void scheduleTimeSync();
  static void timeSyncTask(void* param);
};
