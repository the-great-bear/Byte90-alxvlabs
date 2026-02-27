/**
 * TenclassClient.cpp
 *
 * Implementation for TenclassClient.
 */

#include "TenclassClient.h"
#include "ApiClient.h"
#include "DeviceConfig.h"
#include "NvsStorage.h"
#include "ClockSync.h"

#include <esp_chip_info.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_log.h>
#include <esp_partition.h>

static const char *TAG = "TenclassClient";

TenclassClient::TenclassClient(const String &url, const String &boardType, NVSStorage* storage)
    : _storage(storage)
    , _url(url)
    , _boardType(boardType)
    , _port(443)
    , _useSSL(true)
    , _hasActivationCode(false)
    , _hasActivationChallenge(false)
    , _activationTimeoutMs(0)
    , _hasSerialNumber(false)
    , _hasServerTime(false) {

  // Parse URL
  if (url.startsWith("https://")) {
    _useSSL = true;
    _port = 443;
    int hostStart = 8; // Length of "https://"
    int pathStart = url.indexOf('/', hostStart);

    if (pathStart > 0) {
      String hostPort = url.substring(hostStart, pathStart);
      _path = url.substring(pathStart);

      int portStart = hostPort.indexOf(':');
      if (portStart > 0) {
        _host = hostPort.substring(0, portStart);
        _port = hostPort.substring(portStart + 1).toInt();
      } else {
        _host = hostPort;
      }
    } else {
      _host = url.substring(hostStart);
      _path = "/";
    }
  } else if (url.startsWith("http://")) {
    _useSSL = false;
    _port = 80;
    int hostStart = 7; // Length of "http://"
    int pathStart = url.indexOf('/', hostStart);

    if (pathStart > 0) {
      String hostPort = url.substring(hostStart, pathStart);
      _path = url.substring(pathStart);

      int portStart = hostPort.indexOf(':');
      if (portStart > 0) {
        _host = hostPort.substring(0, portStart);
        _port = hostPort.substring(portStart + 1).toInt();
      } else {
        _host = hostPort;
      }
    } else {
      _host = url.substring(hostStart);
      _path = "/";
    }
  }

  // Read serial number from eFuse
  readSerialNumber();
}

bool TenclassClient::makeRequest(String &response) {
  unsigned long start_ms = millis();
  ESP_LOGI(TAG, "makeRequest(): starting request");

  // Use ApiClient to build device info
  String deviceInfo = ApiClient::buildDeviceInfo(_storage);
  ESP_LOGI(TAG, "[Provisioning] Request payload: %s", deviceInfo.c_str());

  // ✅ Check for serial number (like original code)
  bool hasSerialNumber = false;
  String serialNumber = "";

#ifdef ESP_EFUSE_BLOCK_USR_DATA
  uint8_t serial_data[33] = {0};
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_data, 32 * 8) ==
      ESP_OK) {
    if (serial_data[0] != 0) {
      serialNumber = String((char *)serial_data);
      hasSerialNumber = true;
    }
  }
#endif

  // Set Activation-Version based on serial number (like original)
  String activationVersion = hasSerialNumber ? "2" : "1";

  // Get MAC address for headers
  String macAddr = WiFi.macAddress();
  macAddr.toLowerCase();

  // Get UUID from storage and User-Agent from DeviceConfig
  String uuid = _storage->getDeviceUUID();
  String userAgent = getUserAgent();

  // Log the full request for debugging

  // Build full URL
  String fullUrl = (_useSSL ? "https://" : "http://") + _host;
  if ((_useSSL && _port != 443) || (!_useSSL && _port != 80)) {
    fullUrl += ":" + String(_port);
  }
  fullUrl += _path;

  // Use SecureHttpClient wrapper
  SecureHttpClient http;
  
  if (_useSSL) {
      http.setCertificate(ROOT_CA_CERTIFICATE);
  } else {
      http.setInsecure(true); // Or don't set certificate if not SSL
  }
  
  http.setTimeout(10000);

  // Set headers
  http.addHeader("Activation-Version", activationVersion);
  http.addHeader("Device-Id", macAddr);
  http.addHeader("Client-Id", uuid);
  if (hasSerialNumber) {
    http.addHeader("Serial-Number", serialNumber);
  }
  http.addHeader("User-Agent", userAgent);
  http.addHeader("Accept-Language", DEFAULT_LANGUAGE);
  http.addHeader("Content-Type", "application/json");

  ESP_LOGI(TAG, "makeRequest(): POST %s (timeout: %u ms)",
           fullUrl.c_str(), 10000U);
  // Send POST request
  bool success = http.post(fullUrl, deviceInfo, response);
  int httpResponseCode = http.getResponseCode();
  if (!success) {
    _lastError = "HTTP error: " + String(httpResponseCode);
    ESP_LOGE(TAG, "%s", _lastError.c_str());
    if (response.length() > 0) {
      ESP_LOGI(TAG, "[Provisioning] Response body: %s", response.c_str());
    } else {
      ESP_LOGW(TAG, "[Provisioning] Response body is empty");
    }
    ESP_LOGI(TAG, "makeRequest(): failed after %lu ms",
             millis() - start_ms);
    return false;
  }
  
  ESP_LOGI(TAG, "makeRequest(): response status=%d length=%u, elapsed=%lu ms",
           httpResponseCode,
           static_cast<unsigned int>(response.length()),
           millis() - start_ms);
  if (response.length() > 0) {
    ESP_LOGI(TAG, "[Provisioning] Response body: %s", response.c_str());
  } else {
    ESP_LOGW(TAG, "[Provisioning] Response body is empty");
  }

  return true;
}

bool TenclassClient::parseResponse(const String &response,
                                     ProtocolConfig &config) {
  unsigned long start_ms = millis();
  ESP_LOGI(TAG, "parseResponse(): start (len=%u)",
           static_cast<unsigned int>(response.length()));
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    _lastError = "JSON parse error: " + String(error.c_str());
    ESP_LOGE(TAG, "%s", _lastError.c_str());
    ESP_LOGI(TAG, "parseResponse(): failed after %lu ms",
             millis() - start_ms);
    return false;
  }
  ESP_LOGI(TAG, "parseResponse(): JSON parsed in %lu ms",
           millis() - start_ms);

  // Parse activation if present
  if (doc["activation"].is<JsonObject>()) {
    parseActivation(doc["activation"].as<JsonObject>());
  }

  // Parse server time if present
  if (doc["server_time"].is<JsonObject>()) {
    parseServerTime(doc["server_time"].as<JsonObject>());
  }

  // Parse firmware version
  if (doc["firmware"].is<JsonObject>()) {
    JsonObject firmware = doc["firmware"];
    if (firmware["version"].is<String>()) {
      config.firmwareVersion = firmware["version"].as<String>();
    }
  }

#if !USE_MQTT_PROTOCOL
  // Parse WebSocket configuration (only if using WebSocket protocol)
  if (doc["websocket"].is<JsonObject>()) {
    JsonObject ws = doc["websocket"];

    // **PARSE TOKEN FIRST - ALWAYS**
    if (ws["token"].is<String>()) {
      config.wsToken = ws["token"].as<String>();
      ESP_LOGD(TAG, "WebSocket token found: '%s' (length: %d)",
               config.wsToken.c_str(), config.wsToken.length());
    } else {
      ESP_LOGW(TAG, "WebSocket token NOT found in response");
      config.wsToken = ""; // Explicitly set to empty
    }

    // Parse version (if specified)
    if (ws["version"].is<int>()) {
      config.wsVersion = ws["version"].as<int>();
      ESP_LOGD(TAG, "WebSocket version: %d", config.wsVersion);
    } else {
      config.wsVersion = 1; // default
      ESP_LOGD(TAG, "WebSocket version not specified, using default: 1");
    }

    // Now parse URL
    if (ws["url"].is<String>()) {
      String url = ws["url"].as<String>();
      ESP_LOGD(TAG, "WebSocket URL found: %s", url.c_str());

      // Parse URL: wss://api.tenclass.net/xiaozhi/v1/
      if (url.startsWith("wss://")) {
        config.hasWebSocket = true;
        config.wsUseSSL = true;
        url = url.substring(6); // Remove "wss://"
      } else if (url.startsWith("ws://")) {
        config.hasWebSocket = true;
        config.wsUseSSL = false;
        url = url.substring(5); // Remove "ws://"
      }

      if (config.hasWebSocket) {
        // Extract host, port, and path
        int pathStart = url.indexOf('/');
        String hostPort = (pathStart > 0) ? url.substring(0, pathStart) : url;
        config.wsPath = (pathStart > 0) ? url.substring(pathStart) : "/";

        int portStart = hostPort.indexOf(':');
        if (portStart > 0) {
          config.wsHost = hostPort.substring(0, portStart);
          config.wsPort = hostPort.substring(portStart + 1).toInt();
        } else {
          config.wsHost = hostPort;
          config.wsPort = config.wsUseSSL ? 443 : 80;
        }

        ESP_LOGI(TAG, "WebSocket config: %s://%s:%d%s (SSL: %d)",
                 config.wsUseSSL ? "wss" : "ws", config.wsHost.c_str(),
                 config.wsPort, config.wsPath.c_str(), config.wsUseSSL);
      }
    } else {
      ESP_LOGW(TAG, "WebSocket URL NOT found in response");
    }
  } else {
    ESP_LOGW(TAG, "🟡 No websocket object in response");
  }
#else
  // WebSocket protocol disabled - skip parsing
  ESP_LOGD(TAG, "WebSocket protocol disabled (USE_MQTT_PROTOCOL=1), skipping WebSocket config");
  config.hasWebSocket = false;
#endif

#if USE_MQTT_PROTOCOL
  // Parse MQTT configuration (only if MQTT protocol is enabled)
  if (doc["mqtt"].is<JsonObject>()) {
    JsonObject mqtt = doc["mqtt"];

    // Parse endpoint (format: "host" or "host:port")
    if (mqtt["endpoint"].is<String>()) {
      String endpoint = mqtt["endpoint"].as<String>();
      ESP_LOGI(TAG, "MQTT endpoint found: %s", endpoint.c_str());

      // Parse host and port from endpoint
      int portStart = endpoint.indexOf(':');
      if (portStart > 0) {
        config.mqttBroker = endpoint.substring(0, portStart);
        config.mqttPort = endpoint.substring(portStart + 1).toInt();
      } else {
        config.mqttBroker = endpoint;
        config.mqttPort = 8883; // Default to SSL port (matches original firmware behavior)
      }

      ESP_LOGI(TAG, "MQTT broker: %s:%d", config.mqttBroker.c_str(),
               config.mqttPort);
    }

    // Parse client_id
    if (mqtt["client_id"].is<String>()) {
      config.mqttClientId = mqtt["client_id"].as<String>();
      ESP_LOGD(TAG, "MQTT client ID: %s", config.mqttClientId.c_str());
    }

    // Parse username
    if (mqtt["username"].is<String>()) {
      config.mqttUsername = mqtt["username"].as<String>();
      ESP_LOGD(TAG, "MQTT username: %s", config.mqttUsername.c_str());
    }

    // Parse password
    if (mqtt["password"].is<String>()) {
      config.mqttPassword = mqtt["password"].as<String>();
      ESP_LOGD(TAG, "MQTT password: ***");
    }

    // Parse publish_topic
    if (mqtt["publish_topic"].is<String>()) {
      config.mqttTopicPrefix = mqtt["publish_topic"].as<String>();
      ESP_LOGD(TAG, "MQTT publish topic: %s", config.mqttTopicPrefix.c_str());
    }

    // Parse subscribe_topic (might be "null" string)
    // Note: Store locally since ProtocolConfig doesn't have this field
    // We'll use publish topic for subscribe if subscribe is "null"
    String subscribeTopic = config.mqttTopicPrefix; // Default to publish topic
    if (mqtt["subscribe_topic"].is<String>()) {
      String subTopic = mqtt["subscribe_topic"].as<String>();
      if (subTopic != "null" && subTopic.length() > 0) {
        subscribeTopic = subTopic;
      }
      ESP_LOGD(TAG, "MQTT subscribe topic: %s", subscribeTopic.c_str());
    }

    // ✅ CRITICAL: Set the flag to indicate MQTT is configured
    config.hasMqtt = true; // or config.hasMqttConfig = true; (check your
                           // ProtocolConfig struct)

    ESP_LOGD(TAG, "MQTT configuration parsed successfully");
  } else {
    ESP_LOGW(TAG, "🟡 No mqtt object in response");
    config.hasMqtt = false;
  }
#else
  // MQTT protocol disabled - skip parsing
  ESP_LOGD(TAG, "MQTT protocol disabled (USE_MQTT_PROTOCOL=0), skipping MQTT config");
  config.hasMqtt = false;
#endif

  ESP_LOGI(TAG, "parseResponse(): complete in %lu ms",
           millis() - start_ms);
  return true;
}

bool TenclassClient::checkConfig(ProtocolConfig &config) {
  unsigned long start_ms = millis();
  ESP_LOGI(TAG, "Checking OTA configuration...");

  if (WiFi.status() != WL_CONNECTED) {
    _lastError = "WiFi not connected";
    ESP_LOGE(TAG, "%s", _lastError.c_str());
    return false;
  }

  String response;
  ESP_LOGI(TAG, "checkConfig(): makeRequest() start");
  if (!makeRequest(response)) {
    ESP_LOGI(TAG, "checkConfig(): makeRequest() failed after %lu ms",
             millis() - start_ms);
    return false;
  }
  ESP_LOGI(TAG, "checkConfig(): makeRequest() ok after %lu ms",
           millis() - start_ms);

  ESP_LOGI(TAG, "checkConfig(): parseResponse() start");
  if (!parseResponse(response, config)) {
    ESP_LOGI(TAG, "checkConfig(): parseResponse() failed after %lu ms",
             millis() - start_ms);
    return false;
  }
  ESP_LOGI(TAG, "checkConfig(): parseResponse() ok after %lu ms",
           millis() - start_ms);

  // Store configuration to NVS
  ESP_LOGI(TAG, "checkConfig(): storeConfig() start");
  if (!storeConfig(config)) {
    ESP_LOGW(TAG, "🟡 Failed to store configuration to NVS");
    // Don't fail the whole operation if storage fails
  }
  ESP_LOGI(TAG, "checkConfig(): storeConfig() done after %lu ms",
           millis() - start_ms);

  ESP_LOGI(TAG, "✅ Configuration retrieved successfully in %lu ms",
           millis() - start_ms);
  return true;
}

bool TenclassClient::loadCachedConfig(ProtocolConfig& config) {
  if (!_storage) {
    ESP_LOGW(TAG, "🟡 NVSStorage not available for cached config");
    return false;
  }

  bool loaded = false;

#if USE_MQTT_PROTOCOL
  if (_storage->beginMqtt(true)) {
    Preferences& prefs = _storage->getMqttPrefs();
    String broker = prefs.getString("broker", "");
    String client_id = prefs.getString("client_id", "");
    String username = prefs.getString("username", "");
    String password = prefs.getString("password", "");
    String topic_prefix = prefs.getString("topic_prefix", "");
    int port = prefs.getInt("port", 0);
    _storage->endMqtt();

    if (!broker.isEmpty() && port > 0) {
      config.hasMqtt = true;
      config.mqttBroker = broker;
      config.mqttPort = port;
      config.mqttClientId = client_id;
      config.mqttUsername = username;
      config.mqttPassword = password;
      config.mqttTopicPrefix = topic_prefix;
      loaded = true;
      ESP_LOGI(TAG, "Loaded cached MQTT config from NVS");
    }
  }
#else
  if (_storage->beginWebSocket(true)) {
    Preferences& prefs = _storage->getWebSocketPrefs();
    String host = prefs.getString("host", "");
    String path = prefs.getString("path", "");
    String token = prefs.getString("token", "");
    bool use_ssl = prefs.getBool("use_ssl", true);
    int port = prefs.getInt("port", 0);
    int version = prefs.getInt("version", 1);
    _storage->endWebSocket();

    if (!host.isEmpty() && !token.isEmpty()) {
      config.hasWebSocket = true;
      config.wsHost = host;
      config.wsPath = path.length() > 0 ? path : "/";
      config.wsToken = token;
      config.wsUseSSL = use_ssl;
      config.wsPort = port > 0 ? port : (use_ssl ? 443 : 80);
      config.wsVersion = version > 0 ? version : 1;
      loaded = true;
      ESP_LOGI(TAG, "Loaded cached WebSocket config from NVS");
    }
  }
#endif

  if (!loaded) {
    ESP_LOGW(TAG, "🟡 No cached protocol config found in NVS");
  }

  return loaded;
}

bool TenclassClient::readSerialNumber() {
  _hasSerialNumber = false;
  _serialNumber = "";

#ifdef ESP_EFUSE_BLOCK_USR_DATA
  uint8_t serial_data[33] = {0};
  esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_data, 32 * 8);

  if (err == ESP_OK && serial_data[0] != 0) {
    _serialNumber = String((char *)serial_data);
    _hasSerialNumber = true;
    ESP_LOGI(TAG, "Serial number found: %s", _serialNumber.c_str());
    return true;
  } else {
    ESP_LOGW(TAG, "🟡 No serial number found in eFuse");
  }
#else
  ESP_LOGW(TAG, "eFuse USER_DATA not available on this platform");
#endif

  return false;
}

bool TenclassClient::parseActivation(const JsonObject& activation) {
  _hasActivationChallenge = false;
  _hasActivationCode = false;

  // Parse message
  if (activation["message"].is<String>()) {
    _activationMessage = activation["message"].as<String>();
    ESP_LOGI(TAG, "Activation message: %s", _activationMessage.c_str());
  }

  // Parse code
  if (activation["code"].is<String>()) {
    _activationCode = activation["code"].as<String>();
    _hasActivationCode = true;
    ESP_LOGI(TAG, "Activation code: %s", _activationCode.c_str());
  }

  // Parse challenge
  if (activation["challenge"].is<String>()) {
    _activationChallenge = activation["challenge"].as<String>();
    _hasActivationChallenge = true;
    ESP_LOGI(TAG, "Activation challenge received (length: %d)", _activationChallenge.length());
  }

  // Parse timeout
  if (activation["timeout_ms"].is<int>()) {
    _activationTimeoutMs = activation["timeout_ms"].as<uint32_t>();
    ESP_LOGI(TAG, "Activation timeout: %u ms", _activationTimeoutMs);
  }

  return _hasActivationChallenge || _hasActivationCode;
}

bool TenclassClient::parseServerTime(const JsonObject& serverTime) {
  _hasServerTime = false;

  // Parse timestamp (server sends timestamp in milliseconds)
  if (!serverTime["timestamp"].is<long long>()) {
    ESP_LOGW(TAG, "Server time timestamp not found or invalid");
    return false;
  }

  long long timestamp_ms = serverTime["timestamp"].as<long long>();
  int timezoneOffsetMinutes = 0;

  // Parse timezone offset (optional, in minutes)
  if (serverTime["timezone_offset"].is<int>()) {
    timezoneOffsetMinutes = serverTime["timezone_offset"].as<int>();
  }

  ESP_LOGI(TAG, "Server time received (UTC): %lld ms (offset: %d min); syncing NTP",
           timestamp_ms, timezoneOffsetMinutes);

  ClockSync clock;
  bool synced = clock.syncNow(nullptr, nullptr, 5000);
  _hasServerTime = synced;
  if (!synced) {
    ESP_LOGW(TAG, "⚠️ NTP sync failed after server time receipt");
  }

  return synced;
}

bool TenclassClient::storeConfig(const ProtocolConfig& config) {
  if (!_storage) {
    ESP_LOGE(TAG, "❌ NVSStorage not available");
    return false;
  }

  bool success = true;

#if USE_MQTT_PROTOCOL
  // Store MQTT configuration (only if MQTT protocol is enabled)
  if (config.hasMqtt) {
    if (_storage->beginMqtt(false)) {
      Preferences& prefs = _storage->getMqttPrefs();
      prefs.putString("broker", config.mqttBroker);
      prefs.putInt("port", config.mqttPort);
      prefs.putString("client_id", config.mqttClientId);
      prefs.putString("username", config.mqttUsername);
      prefs.putString("password", config.mqttPassword);
      prefs.putString("topic_prefix", config.mqttTopicPrefix);
      _storage->endMqtt();
      ESP_LOGD(TAG, "MQTT configuration stored to NVS");
    } else {
      ESP_LOGE(TAG, "❌ Failed to open NVS namespace 'mqtt'");
      success = false;
    }
  }
#endif

#if !USE_MQTT_PROTOCOL
  // Store WebSocket configuration (only if WebSocket protocol is enabled)
  if (config.hasWebSocket) {
    if (_storage->beginWebSocket(false)) {
      Preferences& prefs = _storage->getWebSocketPrefs();
      prefs.putString("host", config.wsHost);
      prefs.putInt("port", config.wsPort);
      prefs.putString("path", config.wsPath);
      prefs.putBool("use_ssl", config.wsUseSSL);
      prefs.putString("token", config.wsToken);
      prefs.putInt("version", config.wsVersion);
      _storage->endWebSocket();
      ESP_LOGD(TAG, "WebSocket configuration stored to NVS");
    } else {
      ESP_LOGE(TAG, "❌ Failed to open NVS namespace 'websocket'");
      success = false;
    }
  }
#endif

  return success;
}

String TenclassClient::getActivationPayload() {
  // Use ApiClient to generate activation payload
  return ApiClient::generateActivationPayload(_serialNumber, _activationChallenge);
}

esp_err_t TenclassClient::Activate() {
  if (!_hasActivationChallenge && !_hasActivationCode) {
    ESP_LOGW(TAG, "🟡 No activation challenge or code available");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting activation...");

  // Build activation URL (use full OTA path, not just host)
  // This matches the original firmware behavior: append "activate" to the full OTA URL path
  String activateUrl = (_useSSL ? "https://" : "http://") + _host;
  if ((_useSSL && _port != 443) || (!_useSSL && _port != 80)) {
    activateUrl += ":" + String(_port);
  }
  // Append the OTA path, then "activate"
  if (_path.endsWith("/")) {
    activateUrl += _path + "activate";
  } else {
    activateUrl += _path + "/activate";
  }

  // Get activation payload
  String payload = getActivationPayload();

  // Make HTTP request
  SecureHttpClient http;

  if (_useSSL) {
    http.setInsecure(true); // Activation might use self-signed or we just trust it
  }
  
  http.setTimeout(10000);

  // Set headers
  String macAddr = WiFi.macAddress();
  macAddr.toLowerCase();
  String uuid = _storage->getDeviceUUID();

  http.addHeader("Device-Id", macAddr);
  http.addHeader("Client-Id", uuid);
  http.addHeader("Activation-Version", _hasSerialNumber ? "2" : "1");
  if (_hasSerialNumber) {
    http.addHeader("Serial-Number", _serialNumber);
  }
  http.addHeader("Content-Type", "application/json");

  // Send POST request
  ESP_LOGI(TAG, "[Activation] Sending activation request to: %s", activateUrl.c_str());
  ESP_LOGI(TAG, "[Activation] Request payload: %s", payload.c_str());

  String response;
  bool success = http.post(activateUrl, payload, response);
  int httpResponseCode = http.getResponseCode();
  if (!success) {
    ESP_LOGW(TAG, "[Activation] HTTP request failed before success status check");
  }
  
  ESP_LOGI(TAG, "[Activation] Response status: %d", httpResponseCode);
  
  // Response logging
  if (response.length() > 0) {
    ESP_LOGI(TAG, "[Activation] Response body: %s", response.c_str());
    // Try to parse JSON for better logging
    if (response.startsWith("{") || response.startsWith("[")) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      if (!error) {
        if (doc["message"].is<String>()) {
          ESP_LOGI(TAG, "[Activation] Message: %s", doc["message"].as<String>().c_str());
        }
        if (doc["device_id"].is<int>()) {
          ESP_LOGI(TAG, "[Activation] Device ID: %d", doc["device_id"].as<int>());
        }
        if (doc["error"].is<String>()) {
          ESP_LOGE(TAG, "❌ [Activation] Error: %s", doc["error"].as<String>().c_str());
        }
      }
    }
  } else {
    ESP_LOGW(TAG, "[Activation] Response body is empty");
  }

  // Handle response codes
  if (httpResponseCode == 200) {
    ESP_LOGI(TAG, "[Activation] Activation successful (200 OK)");
    _hasActivationChallenge = false;
    _hasActivationCode = false;
    return ESP_OK;
  } else if (httpResponseCode == 202) {
    ESP_LOGW(TAG, "[Activation] Activation pending (202 Accepted), will retry");
    return ESP_ERR_TIMEOUT;  // Fixed: Triggers retry loop
  } else {
    ESP_LOGE(TAG, "[Activation] Activation failed with code: %d", httpResponseCode);
    _lastError = "Activation failed: " + String(httpResponseCode);
    return ESP_FAIL;
  }
}
