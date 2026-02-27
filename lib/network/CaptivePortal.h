/**
 * CaptivePortal.h
 *
 * Captive portal HTTP/DNS server for WiFi provisioning.
 * Serves assets from LittleFS and exposes JSON APIs for setup.
 *
 * Author: Byte90 Team
 * Board: ESP32-S3 (Seeed Studio XIAO ESP32S3)
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>

class LittleFSManager;
class NVSStorage;
class WifiManager;
class EffectsManager;
class DigitalClockController;
class AudioCodec;

/**
 * @brief Captive portal HTTP/DNS server for WiFi provisioning.
 */
/**
 * CaptivePortal - WiFi provisioning captive portal
 *
 * Features:
 * - DNS hijack to redirect all domains to the AP
 * - WebServer routes for scan/config/status
 * - LittleFS-hosted setup UI assets
 *
 * Usage:
 *   CaptivePortal portal(wifi, storage, filesystem);
 *   portal.begin(ap_ip);
 *   portal.loop();
 */
class CaptivePortal {
public:
    /**
     * @brief Construct a captive portal instance
     * @param wifi WiFi manager instance
     * @param storage NVS storage for credential persistence
     * @param fs LittleFS manager for serving assets
     */
    CaptivePortal(WifiManager* wifi, NVSStorage* storage, LittleFSManager* fs,
                  EffectsManager* effects_manager = nullptr,
                  DigitalClockController* clock_controller = nullptr,
                  AudioCodec* audio_codec = nullptr);
    /**
     * @brief Destroy the captive portal and stop servers
     */
    ~CaptivePortal();

    /**
     * @brief Start the captive portal services
     * @param ap_ip IP address of the AP (for DNS redirect)
     * @return true if started successfully
     */
    bool begin(IPAddress ap_ip);
    /**
     * @brief Process DNS and HTTP requests
     */
    void loop();
    /**
     * @brief Stop the captive portal services
     */
    void stop();
    /**
     * @brief Check if the portal is running
     * @return true when running
     */
    bool isRunning() const { return _running; }
    void setEffectsManager(EffectsManager* effects_manager);
    void setClockController(DigitalClockController* clock_controller);
    void setAudioCodec(AudioCodec* audio_codec);

private:
    void setupRoutes();
    void handleFileRequest();
    void handleScan();
    void handleConfigure();
    void handleStatus();
    void handleDisconnect();
    void handleOpenAiKeyStatus();
    void handleOpenAiKeySave();
    void handleOpenAiKeyClear();
    void handleTimezoneStatus();
    void handleTimezoneSave();
    void handleTimezoneClear();
    void handleTimezoneList();
    void handleLocationStatus();
    void handleLocationSave();
    void handleLocationClear();
    void handleClockStatus();
    void handleClockApply();
    void handleAudioStatus();
    void handleAudioApply();
    void handleAudioReset();
    void handleEffectsStatus();
    void handleEffectsApply();
    void sendJsonResponse(int code, const String& body);
    String getContentType(const String& path) const;
    String authModeToString(wifi_auth_mode_t mode) const;
    String getSignalStrength(int rssi) const;
    String buildJsonResponse(bool success,
                             const String& message,
                             const String& ssid,
                             int rssi,
                             bool connected,
                             const JsonArray& networks);

    WifiManager* _wifi;
    NVSStorage* _storage;
    LittleFSManager* _fs;
    EffectsManager* _effects_manager;
    DigitalClockController* _clock_controller;
    AudioCodec* _audio_codec;

    DNSServer _dns_server;
    WebServer _server;
    bool _running;
    bool _routes_ready;

    String _pending_ssid;
    String _pending_password;
    unsigned long _pending_start_ms;

    static const unsigned long CONNECT_TIMEOUT_MS = 30000;
};
