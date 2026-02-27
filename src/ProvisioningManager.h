/**
 * ProvisioningManager.h
 *
 * Declarations for ProvisioningManager.
 */

#pragma once

#include <functional>

struct ProtocolConfig;

class TenclassClient;
class WifiManager;

/**
 * ProvisioningManager - OTA configuration and activation handling.
 */
class ProvisioningManager {
public:
    ProvisioningManager(
        TenclassClient*& provisioning_client,
        WifiManager*& wifi_client,
        ProtocolConfig& protocol_config,
        bool& config_checked,
        bool& config_check_in_progress,
        std::function<void(bool)> disconnect_callback
    );

    void updateProvisioning();
    void requestConfigRefresh();

private:
    bool _cached_config_loaded;
    static void configCheckTask(void* parameter);
    void startConfigCheckTask();
    void runConfigCheck();

    TenclassClient*& _provisioning_client;
    WifiManager*& _wifi_client;
    ProtocolConfig& _protocol_config;
    bool& _config_checked;
    bool& _config_check_in_progress;
    std::function<void(bool)> _disconnect_callback;
    bool _wifi_disconnect_logged;
};
