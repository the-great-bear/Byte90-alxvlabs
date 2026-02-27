/**
 * WifiManager.h
 *
 * Declarations for WifiManager.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

#include "DeviceConfig.h"

#define WIFI_SSID_MAX_LEN 64
#define WIFI_PASSWORD_MAX_LEN 64

class CaptivePortal;
class EffectsManager;
class DigitalClockController;
class AudioCodec;

/**
 * @brief WifiManager.
 * WifiManager - WiFi connection and network management
 *
 * Manages WiFi connectivity with automatic reconnection, power optimization,
 * and network scanning. Handles connection timeouts and provides callbacks
 * for connection state changes.
 */
class WifiManager {
public:
    WifiManager();
    ~WifiManager();

    /**
     * @brief Initialize WiFi client and configure settings
     */
    bool begin();

    /**
     * @brief Process WiFi events and handle connection timeouts
     */
    void loop();

    // Connection management

    /**
     * @brief Connect to WiFi network with given credentials
     * @param ssid WiFi network name (max 64 characters)
     * @param password WiFi password (max 64 characters)
     */
    bool connect(const char* ssid, const char* password);

    /**
     * @brief Connect to WiFi using credentials stored in NVS
     * @param storage NVS storage instance
     */
    bool connectFromPreferences(class NVSStorage* storage);

    /**
     * @brief Disconnect from current WiFi network
     */
    void disconnect();

    /**
     * @brief Clear cached WiFi credentials used for reconnect
     */
    void clearCredentials();

    /**
     * @brief Reconnect using last known credentials
     */
    bool reconnect();

    /**
     * @brief Check if WiFi is currently connected
     */
    bool isConnected();

    /**
     * @brief Start WiFi Access Point for configuration
     */
    void startAccessPoint();
    void restartAccessPoint();

    /**
     * @brief Configure captive portal for AP provisioning
     * @param storage NVS storage instance
     * @param filesystem LittleFS manager for assets
     */
    void configureCaptivePortal(class NVSStorage* storage, class LittleFSManager* filesystem);
    void setEffectsManager(EffectsManager* effects_manager);
    void setClockController(DigitalClockController* clock_controller);
    void setAudioCodec(AudioCodec* audio_codec);

    // Power management

    /**
     * @brief Stop WiFi radio (disconnects and disables station mode)
     */
    bool stop();

    /**
     * @brief Start WiFi radio
     */
    bool start();
    bool isSuspended() const { return _suspended; }

    /**
     * @brief Adjust WiFi TX power based on signal strength (RSSI)
     */
    void adjustTxPower();

    // Status

    /**
     * @brief Get the SSID of the connected network
     */
    String getSSID();

    /**
     * @brief Get the signal strength (RSSI) of the connection
     */
    int getRSSI();

    /**
     * @brief Get the IP address assigned to this device
     */
    String getIP();

    /**
     * @brief Get the WiFi channel number
     */
    int getChannel();

    /**
     * @brief Get the MAC address of this device
     */
    String getMacAddress();

    /**
     * @brief Get human-readable signal strength description
     */
    String getSignalStrength();

    // Network scanning

    /**
     * @brief Scan for available WiFi networks
     */
    int scanNetworks();

    /**
     * @brief Get SSID of a scanned network
     * @param index Network index from scan results
     */
    String getScannedSSID(int index);

    /**
     * @brief Get RSSI of a scanned network
     * @param index Network index from scan results
     */
    int getScannedRSSI(int index);

    /**
     * @brief Get encryption type of a scanned network
     * @param index Network index from scan results
     */
    wifi_auth_mode_t getScannedEncryption(int index);

    /**
     * @brief Clear scan results from memory
     */
    void clearScan();

    // Callbacks

    typedef std::function<void()> ConnectionCallback;

    /**
     * @brief Register callback for WiFi connected event
     * @param callback Function to call when connected
     */
    void onConnected(ConnectionCallback callback);

    /**
     * @brief Register callback for WiFi disconnected event
     * @param callback Function to call when disconnected
     */
    void onDisconnected(ConnectionCallback callback);

    // Internal state for connection management

    /**
     * @brief Check if connection attempt is in progress
     */
    bool isConnectionInProgress() { return _connectionInProgress; }

    /**
     * @brief Get timestamp when connection attempt started
     */
    unsigned long getConnectionStartTime() { return _connectionStartTime; }

private:
    void setupEventHandlers();
    void onWiFiEventImpl(WiFiEvent_t event, WiFiEventInfo_t info);

    bool _initialized;
    bool _connectionInProgress;
    unsigned long _connectionStartTime;
    int _connectionAttempts;
    BackoffState _backoff;

    ConnectionCallback _onConnected;
    ConnectionCallback _onDisconnected;

    bool _has_credentials;
    bool _no_credentials_logged;
    bool _suspended;
    char _last_ssid[WIFI_SSID_MAX_LEN];
    char _last_password[WIFI_PASSWORD_MAX_LEN];

    class CaptivePortal* _captive_portal;
    EffectsManager* _effects_manager;
    DigitalClockController* _clock_controller;
    AudioCodec* _audio_codec;

    static const int MAX_CONNECTION_ATTEMPTS = 3;
    static const unsigned long CONNECTION_TIMEOUT = 30000;
};
