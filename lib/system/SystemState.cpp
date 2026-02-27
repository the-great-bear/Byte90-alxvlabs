/**
 * SystemState.cpp
 *
 * Implementation for SystemState.
 */

#include "SystemState.h"
#include "DeviceConfig.h"
#include "TaskManager.h"
#include "LanguageManager.h"
#include "NvsStorage.h"
#include "TenclassClient.h"
#include "WifiManager.h"
#include "ClockSync.h"
#include "ClockRtc.h"
#include "I2CManager.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <WiFi.h>

static const char *TAG = "SystemState";

SystemStateManager::SystemStateManager()
    : _state(SYSTEM_STATE_IDLE),
      _previousState(SYSTEM_STATE_UNKNOWN),
      _wifi(nullptr),
      _lastWifiState(false),
      _lang(nullptr),
      _lastStatusLog(0),
      _statusLogInterval(30000)
{
}

bool SystemStateManager::begin(WifiManager* wifi, LanguageManager* lang)
{
    _wifi = wifi;
    _lang = lang;
    _lastWifiState = _wifi ? _wifi->isConnected() : false;
    _lastStatusLog = millis();
    return true;
}

void SystemStateManager::loop() {
    // Monitor WiFi state and handle transitions
    if (_wifi) {
        updateWiFiState();
    }
    // Periodic status logging
    unsigned long now = millis();
    if (now - _lastStatusLog >= _statusLogInterval) {
        _lastStatusLog = now;

        // Print comprehensive status
        printSystemStatus(
            _wifi ? _wifi->isConnected() : false,
            (_wifi && _wifi->isConnected()) ? _wifi->getSSID().c_str() : "",
            WiFi.softAPgetStationNum()
        );
    }
}

void SystemStateManager::setStatusLogInterval(unsigned long interval_ms)
{
    _statusLogInterval = interval_ms;
}

void SystemStateManager::setupWiFiCallbacks()
{
    if (!_wifi) {
        ESP_LOGE(TAG, "❌  WiFi manager not set.");
        return;
    }

    _wifi->onConnected([this]() {
        scheduleTimeSync();
    });
}

bool SystemStateManager::initializeWiFi(NVSStorage* nvs)
{
    if (!_wifi) {
        ESP_LOGE(TAG, "❌  WiFi manager not set.");
        return false;
    }

    // Initialize WiFi hardware
    if (!_wifi->begin()) {
        ESP_LOGE(TAG, "❌  WiFi initialization failed");
        setState(SYSTEM_STATE_WIFI_CONFIGURING);
        return false;
    }

    // Setup WiFi event callbacks for detailed logging
    setupWiFiCallbacks();

    // Always keep AP available for provisioning.
    _wifi->startAccessPoint();

    // Check if we have saved credentials
    if (_wifi->connectFromPreferences(nvs)) {
        setState(SYSTEM_STATE_CONNECTING);
    } else {
        setState(SYSTEM_STATE_WIFI_CONFIGURING);
    }

    return true;
}

// System state management
void SystemStateManager::setState(SystemState state) {
  if (_state == state)
    return;

  _previousState = _state;
  _state = state;

  // Log state transition with context
  size_t free_heap = ESP.getFreeHeap();
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  bool wifi_connected = _wifi ? _wifi->isConnected() : false;

  ESP_LOGI(TAG, "[SystemState] State: %s -> %s | WiFi: %s | Heap: %uKB | PSRAM: %uKB",
           SystemStateToString(_previousState),
           SystemStateToString(state),
           wifi_connected ? "ON" : "OFF",
           free_heap / 1024,
           free_psram / 1024);

  if (state != SYSTEM_STATE_CONNECTING) {
    TaskManager::instance().printHealthReport();
  }

  // Post state change event to all callbacks
  postStateChangeEvent(_previousState, state);
}

const char *SystemStateManager::getStateString(SystemState state) {
  return SystemStateToString(state);
}

void SystemStateManager::printSystemStatus(bool wifi_connected, const char* wifi_ssid, int ap_clients)
{
    // Get memory information
    size_t free_heap = ESP.getFreeHeap();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    // Build WiFi status string
    String wifi_status = wifi_connected ? "ON" : "OFF";
    if (wifi_connected && wifi_ssid && strlen(wifi_ssid) > 0) {
        wifi_status += " (";
        wifi_status += wifi_ssid;
        wifi_status += ")";
    }

    // Get current language
    const char* current_lang = (_lang && _lang->isReady())
        ? _lang->getCurrentLanguage()
        : "Unknown";

    ESP_LOGD(TAG, ":::: System Status ::::");
    ESP_LOGD(TAG, "State: %s | Language: %s",
             SystemStateToString(_state),
             current_lang);
    ESP_LOGD(TAG, "AP Clients: %d | WiFi: %s",
             ap_clients,
             wifi_status.c_str());
    ESP_LOGD(TAG, "Memory - Heap: %dKB (Block: %dKB) | PSRAM: %dKB | Internal: %dKB",
             free_heap / 1024,
             largest_block / 1024,
             free_psram / 1024,
             free_internal / 1024);
}

// State change event callback registration
void SystemStateManager::registerStateChangeCallback(
    std::function<void(SystemState, SystemState)> callback) {
  _stateChangeCallbacks.push_back(callback);
  ESP_LOGD(TAG, "Registered state change callback (total: %d)",
           _stateChangeCallbacks.size());
}

// Post state change event (invokes all callbacks)
void SystemStateManager::postStateChangeEvent(SystemState previous_state,
                                              SystemState current_state) {
  ESP_LOGD(TAG, "Posting state change event: %s -> %s (callbacks: %d)",
           SystemStateToString(previous_state),
           SystemStateToString(current_state), _stateChangeCallbacks.size());

  // Invoke all registered callbacks
  for (const auto &callback : _stateChangeCallbacks) {
    if (callback) {
      callback(previous_state, current_state);
    }
  }
}

void SystemStateManager::updateWiFiState()
{
    if (!_wifi) return;

    if (_wifi->isSuspended()) {
        _lastWifiState = false;
        return;
    }

    bool current_wifi_state = _wifi->isConnected();

    if (current_wifi_state != _lastWifiState) {
        if (current_wifi_state) {
            // Transition to IDLE when WiFi successfully connects
            if (_state == SYSTEM_STATE_CONNECTING) {
                setState(SYSTEM_STATE_IDLE);
            }
        } else {
            // Handle WiFi disconnection based on current state
            if (_state == SYSTEM_STATE_IDLE ||
                _state == SYSTEM_STATE_LISTENING ||
                _state == SYSTEM_STATE_SPEAKING) {
                // Lost connection during operation - try to reconnect
                setState(SYSTEM_STATE_CONNECTING);
            }
        }
        _lastWifiState = current_wifi_state;
    }

    if (!current_wifi_state && _state == SYSTEM_STATE_CONNECTING &&
        !_wifi->isConnectionInProgress()) {
        _wifi->reconnect();
    }
}

void SystemStateManager::scheduleTimeSync() {
    if (TaskManager::instance().isTaskActive("time_sync")) {
        return;
    }

    bool created = TaskManager::instance().createTask(
        "time_sync",
        "SystemState",
        timeSyncTask,
        this,
        1,
        1,
        6144,
        CleanupPattern::SELF_DELETING,
        "NTP sync + RTC update on WiFi connect"
    );
    if (!created) {
        ESP_LOGW(TAG, "Time sync task already active");
    }
}

void SystemStateManager::timeSyncTask(void* param) {
    SystemStateManager* manager = static_cast<SystemStateManager*>(param);
    if (!manager || !manager->_wifi) {
        TaskManager::instance().markTaskStopped("time_sync");
        vTaskDelete(nullptr);
        return;
    }

    ClockSync clock_sync;
    bool synced = clock_sync.syncNow(nullptr, nullptr, 5000);
    if (!synced) {
        ESP_LOGW(TAG, "NTP sync failed after WiFi connect");
    } else {
        ClockRtc rtc;
        if (rtc.begin(I2CManager::getInstance())) {
            if (rtc.syncFromSystemTime()) {
                ESP_LOGI(TAG, "RTC synced from system time");
            } else {
                ESP_LOGW(TAG, "RTC sync skipped (system time invalid)");
            }
        } else {
            ESP_LOGW(TAG, "RTC not available; skipping sync");
        }
    }

    TaskManager::instance().markTaskStopped("time_sync");
    vTaskDelete(nullptr);
}
