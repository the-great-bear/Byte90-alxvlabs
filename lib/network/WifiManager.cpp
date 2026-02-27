/**
 * WifiManager.cpp
 *
 * Implementation for WifiManager.
 */

#include "WifiManager.h"
#include "CaptivePortal.h"
#include "DeviceConfig.h"
#include "DeviceSimulator.h"
#include "DigitalClockController.h"
#include "EffectsManager.h"
#include "AudioCodec.h"
#include "LittlefsManager.h"
#include "NvsStorage.h"
#include <esp_log.h>
#include <esp_wifi.h>

static const char* TAG = "WifiManager";

WifiManager::WifiManager()
    : _initialized(false),
      _connectionInProgress(false),
      _connectionStartTime(0),
      _connectionAttempts(0),
      _has_credentials(false),
      _no_credentials_logged(false),
      _suspended(false),
      _captive_portal(nullptr),
      _effects_manager(nullptr),
      _clock_controller(nullptr),
      _audio_codec(nullptr)
{
    _backoff.reset();
    _last_ssid[0] = '\0';
    _last_password[0] = '\0';
}

WifiManager::~WifiManager()
{
    if (_captive_portal) {
        delete _captive_portal;
        _captive_portal = nullptr;
    }
}

bool WifiManager::begin()
{
    ESP_LOGI(TAG, "Initializing...");

    // Configure WiFi for manual control
    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_MODE_NULL);
    WiFi.setTxPower(WIFI_POWER_19dBm);

    setupEventHandlers();

    _initialized = true;
    ESP_LOGI(TAG, "✅  WiFi Client initialized");
    return true;
}

void WifiManager::setupEventHandlers()
{
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        this->onWiFiEventImpl(event, info);
    });
}

void WifiManager::onWiFiEventImpl(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGD(TAG, "WiFi station started");
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGD(TAG, "Connected to WiFi network");
            _connectionInProgress = false;
            _connectionAttempts = 0;
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "Got IP: %s (after %d attempts)",
                     WiFi.localIP().toString().c_str(),
                     _backoff.attempt_count);
            _backoff.reset();
            if (_onConnected) {
                _onConnected();
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from WiFi (reason: %d), attempt %d, next retry in %d ms",
                     info.wifi_sta_disconnected.reason,
                     _backoff.attempt_count,
                     _backoff.current_delay_ms);
            _connectionInProgress = false;
            _backoff.increment();
            if (_onDisconnected) {
                _onDisconnected();
            }
            break;

        default:
            break;
    }
}

void WifiManager::loop()
{
    // Adjust TX power every 10 seconds based on signal strength
    static unsigned long last_power_check = 0;
    if (millis() - last_power_check > 10000) {
        adjustTxPower();
        last_power_check = millis();
    }

    // Handle connection timeout
    if (_connectionInProgress) {
        if (millis() - _connectionStartTime > CONNECTION_TIMEOUT) {
            ESP_LOGW(TAG, "Connection timeout");
            _connectionInProgress = false;
            _connectionAttempts++;
        }
    }

    if (_captive_portal) {
        _captive_portal->loop();
    }
}

bool WifiManager::connect(const char* ssid, const char* password)
{
    if (_suspended) {
        ESP_LOGW(TAG, "WiFi connect blocked (suspended)");
        return false;
    }
    if (!_initialized) {
        ESP_LOGE(TAG, "WiFi Client not initialized");
        return false;
    }

    // Check backoff before attempting connection
    unsigned long now = millis();
    if (_backoff.attempt_count > 0 && !_backoff.shouldRetry(now)) {
        uint32_t wait_time = _backoff.current_delay_ms - (now - _backoff.last_attempt_time);
        ESP_LOGD(TAG, "Backoff active, retry in %d ms", wait_time);
        return false;
    }

    // Validation
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) >= WIFI_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "Invalid SSID");
        return false;
    }

    if (password && strlen(password) >= WIFI_PASSWORD_MAX_LEN) {
        ESP_LOGE(TAG, "Password too long");
        return false;
    }

    strncpy(_last_ssid, ssid, WIFI_SSID_MAX_LEN - 1);
    _last_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
    if (password) {
        strncpy(_last_password, password, WIFI_PASSWORD_MAX_LEN - 1);
        _last_password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';
    } else {
        _last_password[0] = '\0';
    }
    _has_credentials = true;
    _no_credentials_logged = false;

    // Check if already connected to this network
    if (isConnected() && WiFi.SSID().equals(ssid)) {
        ESP_LOGI(TAG, "Already connected to %s", ssid);
        return true;
    }

    // Enable WiFi
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_MODE_STA);
        delay(100);
    }

    // Disconnect if needed
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(false);
        delay(100);
    }

    // Start connection
    ESP_LOGI(TAG, "Connecting to: %s (attempt %d)", ssid, _backoff.attempt_count + 1);
    WiFi.begin(ssid, password);

    _connectionInProgress = true;
    _connectionStartTime = now;
    _backoff.last_attempt_time = now;

    return true;
}

bool WifiManager::connectFromPreferences(NVSStorage* storage)
{
    if (!storage) {
        ESP_LOGE(TAG, "NVS storage not available");
        return false;
    }

    wifi_credentials_t credentials;
    if (!storage->loadWiFiCredentials(&credentials)) {
        ESP_LOGW(TAG, "No saved credentials");
        return false;
    }

    return connect(credentials.ssid, credentials.password);
}

void WifiManager::disconnect()
{
    if (WiFi.status() == WL_CONNECTED || _connectionInProgress) {
        ESP_LOGI(TAG, "Disconnecting from WiFi");
        WiFi.disconnect();
        _connectionInProgress = false;
        _connectionAttempts = 0;
    }
}

bool WifiManager::stop()
{
    disconnect();
    _suspended = true;
    WiFi.mode(WIFI_MODE_NULL);
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WiFi stopped");
    return true;
}

bool WifiManager::start()
{
    if (!_initialized) {
        ESP_LOGW(TAG, "WiFi not initialized, starting init");
        begin();
    }
    _suspended = false;
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WiFi started");
    return true;
}

void WifiManager::clearCredentials()
{
    _has_credentials = false;
    _no_credentials_logged = false;
    _last_ssid[0] = '\0';
    _last_password[0] = '\0';
}

bool WifiManager::reconnect()
{
    if (_suspended) {
        ESP_LOGD(TAG, "WiFi reconnect skipped (suspended)");
        return false;
    }
    if (!_has_credentials) {
        if (!_no_credentials_logged) {
            ESP_LOGW(TAG, "No cached WiFi credentials to reconnect");
            _no_credentials_logged = true;
        }
        return false;
    }

    return connect(_last_ssid, _last_password);
}

bool WifiManager::isConnected()
{
    if (DeviceSimulator::isWifiSimEnabled()) {
        return DeviceSimulator::readWifiConnected();
    }

    if (_suspended) {
        return false;
    }
    return (WiFi.getMode() == WIFI_MODE_STA || WiFi.getMode() == WIFI_MODE_APSTA) &&
           (WiFi.status() == WL_CONNECTED);
}

String WifiManager::getSSID()
{
    return isConnected() ? WiFi.SSID() : "";
}

int WifiManager::getRSSI()
{
    return isConnected() ? WiFi.RSSI() : 0;
}

String WifiManager::getIP()
{
    return isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
}

int WifiManager::getChannel()
{
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    return primary;
}

String WifiManager::getMacAddress()
{
    return WiFi.macAddress();
}

String WifiManager::getSignalStrength()
{
    if (!isConnected()) {
        return "NOT_CONNECTED";
    }

    int rssi = WiFi.RSSI();
    if (rssi >= -30) return "Excellent";
    else if (rssi >= -50) return "Good";
    else if (rssi >= -70) return "Fair";
    else if (rssi >= -90) return "Poor";
    else return "Very Poor";
}

void WifiManager::startAccessPoint() {
  ESP_LOGI(TAG, "Starting Access Point...");
  WiFi.mode(WIFI_MODE_APSTA); // AP+STA mode to maintain WiFi connection
  delay(100);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGateway, apMask);

  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);

  if (_captive_portal) {
      _captive_portal->begin(apIP);
  }
}

void WifiManager::restartAccessPoint() {
  ESP_LOGI(TAG, "Restarting Access Point...");
  WiFi.softAPdisconnect(true);
  delay(100);
  startAccessPoint();
}

void WifiManager::configureCaptivePortal(NVSStorage* storage, LittleFSManager* filesystem)
{
    if (_captive_portal) {
        delete _captive_portal;
        _captive_portal = nullptr;
    }

    if (storage && filesystem) {
        _captive_portal = new CaptivePortal(this, storage, filesystem, _effects_manager,
                                            _clock_controller, _audio_codec);
    }
}

void WifiManager::setEffectsManager(EffectsManager* effects_manager) {
    _effects_manager = effects_manager;
    if (_captive_portal) {
        _captive_portal->setEffectsManager(effects_manager);
    }
}

void WifiManager::setClockController(DigitalClockController* clock_controller) {
    _clock_controller = clock_controller;
    if (_captive_portal) {
        _captive_portal->setClockController(clock_controller);
    }
}

void WifiManager::setAudioCodec(AudioCodec* audio_codec) {
    _audio_codec = audio_codec;
    if (_captive_portal) {
        _captive_portal->setAudioCodec(audio_codec);
    }
}

int WifiManager::scanNetworks()
{
    ESP_LOGI(TAG, "Scanning for networks...");
    return WiFi.scanNetworks(false);
}

String WifiManager::getScannedSSID(int index)
{
    return WiFi.SSID(index);
}

int WifiManager::getScannedRSSI(int index)
{
    return WiFi.RSSI(index);
}

wifi_auth_mode_t WifiManager::getScannedEncryption(int index)
{
    return WiFi.encryptionType(index);
}

void WifiManager::clearScan()
{
    WiFi.scanDelete();
}

void WifiManager::onConnected(ConnectionCallback callback)
{
    _onConnected = callback;
}

void WifiManager::onDisconnected(ConnectionCallback callback)
{
    _onDisconnected = callback;
}

void WifiManager::adjustTxPower()
{
    if (!isConnected()) {
        return;
    }

    int rssi = WiFi.RSSI();
    wifi_power_t power = (rssi >= -50) ? WIFI_POWER_8_5dBm :
                         (rssi >= -70) ? WIFI_POWER_11dBm : WIFI_POWER_19dBm;

    WiFi.setTxPower(power);
}
