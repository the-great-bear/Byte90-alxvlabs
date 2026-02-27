/**
 * ProvisioningManager.cpp
 *
 * Implementation for ProvisioningManager.
 */

#include "ProvisioningManager.h"

#include "TaskManager.h"
#include "TenclassClient.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <esp_err.h>
#include <esp_log.h>

static const char* TAG = "ProvisioningManager";

ProvisioningManager::ProvisioningManager(
    TenclassClient*& provisioning_client,
    WifiManager*& wifi_client,
    ProtocolConfig& protocol_config,
    bool& config_checked,
    bool& config_check_in_progress,
    std::function<void(bool)> disconnect_callback
)
    : _provisioning_client(provisioning_client)
    , _wifi_client(wifi_client)
    , _protocol_config(protocol_config)
    , _config_checked(config_checked)
    , _config_check_in_progress(config_check_in_progress)
    , _disconnect_callback(disconnect_callback)
    , _cached_config_loaded(false)
    , _wifi_disconnect_logged(false) {
}

void ProvisioningManager::updateProvisioning() {
    if (_wifi_client->isConnected()) {
        _wifi_disconnect_logged = false;

        if (!_config_checked && !_config_check_in_progress) {
            if (!_cached_config_loaded && _provisioning_client) {
                _cached_config_loaded = _provisioning_client->loadCachedConfig(_protocol_config);
                if (_cached_config_loaded) {
                    _config_checked = true;
                    ESP_LOGI(TAG, "✅ Using cached provisioning config");
                }
            }

            if (!_config_checked) {
                startConfigCheckTask();
            }
        }
    } else {
        if (!_wifi_disconnect_logged) {
            if (_config_checked) {
                ESP_LOGI(TAG, "WiFi disconnected - will check OTA config on next connection");
                _config_checked = false;
            }

            if (_disconnect_callback) {
                ESP_LOGI(TAG, "WiFi disconnected - closing protocol connection");
                _disconnect_callback(false);
            }

            _wifi_disconnect_logged = true;
        }
    }
}

void ProvisioningManager::requestConfigRefresh() {
    if (_config_check_in_progress) {
        return;
    }

    _config_checked = false;
    _cached_config_loaded = false;
    if (_wifi_client && _wifi_client->isConnected()) {
        startConfigCheckTask();
    }
}

void ProvisioningManager::configCheckTask(void* parameter) {
    ProvisioningManager* self = static_cast<ProvisioningManager*>(parameter);
    if (self) {
        self->runConfigCheck();
    }
    TaskManager::instance().markTaskStopped("config_check");
    vTaskDelete(nullptr);
}

void ProvisioningManager::startConfigCheckTask() {
    if (_config_check_in_progress ||
        TaskManager::instance().isTaskActive("config_check")) {
        return;
    }

    _config_check_in_progress = true;

    bool created = TaskManager::instance().createTask(
        "config_check",
        "ProvisioningManager",
        ProvisioningManager::configCheckTask,
        this,
        1,                      // Priority
        0,                      // Core 0
        8192,                   // 8KB stack
        CleanupPattern::SELF_DELETING,
        "Provisioning configuration check"
    );
    if (!created) {
        _config_check_in_progress = false;
        ESP_LOGE(TAG, "Failed to start config check task");
    }
}

void ProvisioningManager::runConfigCheck() {
    unsigned long start_time = millis();
    bool success = _provisioning_client->checkConfig(_protocol_config);
    unsigned long duration = millis() - start_time;

    if (success) {
        if (_provisioning_client->HasActivationChallenge() ||
            _provisioning_client->HasActivationCode()) {
            ESP_LOGI(TAG, "⚠ Activation required!");

            String activationMessage = _provisioning_client->GetActivationMessage();
            String activationCode = _provisioning_client->GetActivationCode();

            (void)activationMessage;
            (void)activationCode;

            for (int i = 0; i < 10; ++i) {
                ESP_LOGI(TAG, "Activating... %d/10", i + 1);
                esp_err_t err = _provisioning_client->Activate();

                if (err == ESP_OK) {
                    break;
                } else if (err == ESP_ERR_TIMEOUT) {
                    delay(3000);
                } else {
                    delay(10000);
                }
            }
        }

        _config_checked = true;
        ESP_LOGI(TAG, "✅ Configuration ready (will connect on button press)");
    } else {
        ESP_LOGE(TAG, "\n❌  Configuration check failed in %lu ms", duration);
        ESP_LOGE(TAG, "Error: %s", _provisioning_client->getLastError().c_str());
        ESP_LOGE(TAG, "\nWill retry on next WiFi connection...\n");
    }

    _config_check_in_progress = false;
}
