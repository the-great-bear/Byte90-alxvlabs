/**
 * CaptivePortal.cpp
 *
 * Implementation for captive portal provisioning.
 *
 * Author: Byte90 Team
 * Board: ESP32-S3 (Seeed Studio XIAO ESP32S3)
 */

#include "CaptivePortal.h"
#include "ClockSync.h"
#include "DeviceConfig.h"
#include "DigitalClockController.h"
#include "AudioCodec.h"
#include "EffectsManager.h"
#include "LittlefsManager.h"
#include "NvsStorage.h"
#include "RetroTints.h"
#include "WifiManager.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "CaptivePortal";

CaptivePortal::CaptivePortal(WifiManager* wifi, NVSStorage* storage, LittleFSManager* fs,
                             EffectsManager* effects_manager,
                             DigitalClockController* clock_controller,
                             AudioCodec* audio_codec)
    : _wifi(wifi)
    , _storage(storage)
    , _fs(fs)
    , _effects_manager(effects_manager)
    , _clock_controller(clock_controller)
    , _audio_codec(audio_codec)
    , _dns_server()
    , _server(80)
    , _running(false)
    , _routes_ready(false)
    , _pending_ssid("")
    , _pending_password("")
    , _pending_start_ms(0)
{
}

CaptivePortal::~CaptivePortal() {
    stop();
}

bool CaptivePortal::begin(IPAddress ap_ip) {
    if (_running) {
        return true;
    }

    if (!_routes_ready) {
        setupRoutes();
        _routes_ready = true;
    }

    if (!_dns_server.start(53, "*", ap_ip)) {
        ESP_LOGW(TAG, "Failed to start DNS server");
    }

    _server.begin();
    _running = true;
    ESP_LOGI(TAG, "Captive portal started");
    return true;
}

void CaptivePortal::stop() {
    if (!_running) {
        return;
    }
    _dns_server.stop();
    _server.stop();
    _running = false;
    ESP_LOGI(TAG, "Captive portal stopped");
}

void CaptivePortal::setEffectsManager(EffectsManager* effects_manager) {
    _effects_manager = effects_manager;
}

void CaptivePortal::setClockController(DigitalClockController* clock_controller) {
    _clock_controller = clock_controller;
}

void CaptivePortal::setAudioCodec(AudioCodec* audio_codec) {
    _audio_codec = audio_codec;
}

void CaptivePortal::loop() {
    if (!_running) {
        return;
    }

    _dns_server.processNextRequest();
    _server.handleClient();

    if (_pending_ssid.length() > 0 && _wifi && _wifi->isConnected()) {
        if (_wifi->getSSID().equals(_pending_ssid)) {
            if (_storage) {
                _storage->saveWiFiCredentials(_pending_ssid.c_str(), _pending_password.c_str());
            }
            _pending_ssid = "";
            _pending_password = "";
        }
    }

    if (_pending_ssid.length() > 0 &&
        (millis() - _pending_start_ms) > CONNECT_TIMEOUT_MS) {
        _pending_ssid = "";
        _pending_password = "";
    }
}

void CaptivePortal::setupRoutes() {
    _server.on("/api/scan", HTTP_GET, [this]() { handleScan(); });
    _server.on("/api/configure", HTTP_POST, [this]() { handleConfigure(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/disconnect", HTTP_POST, [this]() { handleDisconnect(); });
    _server.on("/api/openai-key/status", HTTP_GET, [this]() { handleOpenAiKeyStatus(); });
    _server.on("/api/openai-key", HTTP_POST, [this]() { handleOpenAiKeySave(); });
    _server.on("/api/openai-key/clear", HTTP_POST, [this]() { handleOpenAiKeyClear(); });
    _server.on("/api/timezone/status", HTTP_GET, [this]() { handleTimezoneStatus(); });
    _server.on("/api/timezone/list", HTTP_GET, [this]() { handleTimezoneList(); });
    _server.on("/api/timezone", HTTP_POST, [this]() { handleTimezoneSave(); });
    _server.on("/api/timezone/clear", HTTP_POST, [this]() { handleTimezoneClear(); });
    _server.on("/api/location/status", HTTP_GET, [this]() { handleLocationStatus(); });
    _server.on("/api/location", HTTP_POST, [this]() { handleLocationSave(); });
    _server.on("/api/location/clear", HTTP_POST, [this]() { handleLocationClear(); });
    _server.on("/api/clock/status", HTTP_GET, [this]() { handleClockStatus(); });
    _server.on("/api/clock", HTTP_POST, [this]() { handleClockApply(); });
    _server.on("/api/audio/status", HTTP_GET, [this]() { handleAudioStatus(); });
    _server.on("/api/audio", HTTP_POST, [this]() { handleAudioApply(); });
    _server.on("/api/audio/reset", HTTP_POST, [this]() { handleAudioReset(); });
    _server.on("/api/effects/status", HTTP_GET, [this]() { handleEffectsStatus(); });
    _server.on("/api/effects", HTTP_POST, [this]() { handleEffectsApply(); });
    _server.on("/portal", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/portal/", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/hotspot-detect.html", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/generate_204", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/fwlink", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/ncsi.txt", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/favicon.ico", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.on("/", HTTP_ANY, [this]() { handleFileRequest(); });
    _server.onNotFound([this]() {
        if (_server.method() == HTTP_OPTIONS) {
            _server.send(204);
            return;
        }
        handleFileRequest();
    });
}

void CaptivePortal::handleFileRequest() {
    if (!_fs || !_fs->isReady()) {
        _server.send(500, "text/plain", "Filesystem not ready");
        return;
    }

    String path = _server.uri();
    if (path.startsWith("/assets/")) {
        path = path.substring(strlen("/assets"));
    }

    if (path == "/" ||
        path == "/portal" ||
        path == "/hotspot-detect.html" ||
        path == "/generate_204" ||
        path == "/fwlink" ||
        path == "/ncsi.txt") {
        path = "/portal/index.html";
    } else if (!path.startsWith("/portal/")) {
        path = "/portal" + path;
    }

    if (!_fs->exists(path.c_str())) {
        path = "/portal/index.html";
        if (!_fs->exists(path.c_str())) {
            _server.send(404, "text/plain", "Not found");
            return;
        }
    }

    File file = _fs->open(path.c_str(), "r");
    if (!file) {
        _server.send(500, "text/plain", "Failed to open file");
        return;
    }
    size_t file_size = file.size();
    ESP_LOGI(TAG, "Serving portal file: %s (%u bytes)", path.c_str(),
             static_cast<unsigned int>(file_size));
    if (file_size == 0) {
        file.close();
        _server.send(404, "text/plain", "Portal file missing");
        return;
    }

    String content_type = getContentType(path);
    bool is_gzip = path.endsWith(".gz");
    WiFiClient client = _server.client();
    if (!client.connected()) {
        file.close();
        return;
    }

    String headers = "HTTP/1.1 200 OK\r\n";
    headers += "Content-Type: " + content_type + "\r\n";
    headers += "Content-Length: " + String(file_size) + "\r\n";
    if (is_gzip && content_type != "application/x-gzip" &&
        content_type != "application/octet-stream") {
        headers += "Content-Encoding: gzip\r\n";
    }
    // iOS captive portal can cache aggressively; disable caching to avoid stale loads.
    headers += "Cache-Control: no-store\r\n";
    headers += "Pragma: no-cache\r\n";
    headers += "Expires: 0\r\n";
    headers += "Connection: close\r\n\r\n";
    client.write(reinterpret_cast<const uint8_t*>(headers.c_str()),
                 headers.length());

    if (_server.method() == HTTP_HEAD) {
        file.close();
        return;
    }

    const size_t buffer_size = 2048;
    uint8_t buffer[buffer_size];
    while (file.available() && client.connected()) {
        size_t bytes_read = file.read(buffer, buffer_size);
        if (bytes_read == 0) {
            break;
        }
        client.write(buffer, bytes_read);
        esp_task_wdt_reset();
        delay(0);
    }
    file.close();
}

void CaptivePortal::handleScan() {
    if (!_wifi) {
        JsonDocument empty_doc;
        JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
        sendJsonResponse(
            500,
            buildJsonResponse(false, "WiFi not ready", "", 0, false, empty_networks)
        );
        return;
    }

    int count = _wifi->scanNetworks();
    if (count < 0) {
        JsonDocument empty_doc;
        JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
        sendJsonResponse(
            500,
            buildJsonResponse(false, "Scan failed", "", 0, false, empty_networks)
        );
        return;
    }
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject item = networks.add<JsonObject>();
        int rssi = _wifi->getScannedRSSI(i);
        wifi_auth_mode_t enc = _wifi->getScannedEncryption(i);
        item["ssid"] = _wifi->getScannedSSID(i);
        item["rssi"] = rssi;
        item["signal_strength"] = getSignalStrength(rssi);
        item["encryption_type"] = (int)enc;
        item["is_open"] = (enc == WIFI_AUTH_OPEN);
        item["security"] = authModeToString(enc);
    }
    _wifi->clearScan();

    String message = "Found " + String(count) + " networks";
    sendJsonResponse(
        200,
        buildJsonResponse(true, message, "", 0, _wifi->isConnected(), networks)
    );
}

void CaptivePortal::handleConfigure() {
    String ssid = _server.arg("ssid");
    String password = _server.arg("password");

    if (ssid.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err) {
                ssid = doc["ssid"] | "";
                password = doc["password"] | "";
            }
        }
    }

    ssid.trim();
    password.trim();

    if (ssid.isEmpty()) {
        JsonDocument empty_doc;
        JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
        sendJsonResponse(
            400,
            buildJsonResponse(false, "SSID required", "", 0, false, empty_networks)
        );
        return;
    }

    if (_wifi) {
        if (_wifi->isConnected()) {
            _wifi->disconnect();
        }
        _wifi->connect(ssid.c_str(), password.c_str());
    }

    _pending_ssid = ssid;
    _pending_password = password;
    _pending_start_ms = millis();

    JsonDocument empty_doc;
    JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
    sendJsonResponse(
        200,
        buildJsonResponse(true, "Connecting to " + ssid, ssid, 0, false, empty_networks)
    );
}

void CaptivePortal::handleStatus() {
    bool connected = _wifi && _wifi->isConnected();
    String ssid = connected && _wifi ? _wifi->getSSID() : "";
    int rssi = connected && _wifi ? _wifi->getRSSI() : 0;
    String message;
    if (connected && ssid.length() > 0) {
        message = "Connected to " + ssid;
    } else if (_pending_ssid.length() > 0) {
        message = "Connecting to " + _pending_ssid;
        ssid = _pending_ssid;
    } else {
        message = "Not connected";
    }

    JsonDocument empty_doc;
    JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
    sendJsonResponse(
        200,
        buildJsonResponse(true, message, ssid, rssi, connected, empty_networks)
    );
}

void CaptivePortal::handleDisconnect() {
    if (!_wifi) {
        JsonDocument empty_doc;
        JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
        sendJsonResponse(
            500,
            buildJsonResponse(false, "WiFi not ready", "", 0, false, empty_networks)
        );
        return;
    }

    bool was_connected = _wifi->isConnected();
    String current_ssid = _wifi->getSSID();
    _wifi->disconnect();
    _wifi->clearCredentials();

    if (_storage) {
        _storage->clearWiFiCredentials();
    }

    _pending_ssid = "";
    _pending_password = "";
    _pending_start_ms = 0;

    String message = was_connected
        ? "Disconnected and cleared credentials for " + current_ssid
        : "Cleared saved WiFi credentials";

    JsonDocument empty_doc;
    JsonArray empty_networks = empty_doc["networks"].to<JsonArray>();
    sendJsonResponse(
        200,
        buildJsonResponse(true, message, "", 0, false, empty_networks)
    );
}

void CaptivePortal::handleOpenAiKeyStatus() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        doc["supported"] = true;
        doc["has_key"] = false;
        doc["last4"] = "";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    bool has_key = _storage->hasOpenAiApiKey();
    String last4 = has_key ? _storage->getOpenAiApiKeyLast4() : "";

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = has_key ? "API key stored" : "No API key stored";
    doc["supported"] = true;
    doc["has_key"] = has_key;
    doc["last4"] = last4;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleOpenAiKeySave() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String api_key = _server.arg("api_key");
    if (api_key.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err && doc["api_key"].is<String>()) {
                api_key = doc["api_key"].as<String>();
            }
        }
    }

    if (api_key.isEmpty()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "API key required";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    bool saved = _storage->saveOpenAiApiKey(api_key.c_str());
    JsonDocument doc;
    doc["success"] = saved;
    doc["message"] = saved
        ? "API key has been stored successfully"
        : "Failed to store API key";
    doc["has_key"] = saved;
    doc["last4"] = saved ? _storage->getOpenAiApiKeyLast4() : "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(saved ? 200 : 500, response);
}

void CaptivePortal::handleOpenAiKeyClear() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    if (!_storage->hasOpenAiApiKey()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "No existing key saved";
        doc["has_key"] = false;
        doc["last4"] = "";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    bool cleared = _storage->clearOpenAiApiKey();
    JsonDocument doc;
    doc["success"] = cleared;
    doc["message"] = cleared
        ? "API key has been cleared successfully"
        : "Failed to clear API key";
    doc["has_key"] = false;
    doc["last4"] = "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(cleared ? 200 : 500, response);
}

void CaptivePortal::handleTimezoneStatus() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        doc["timezone_name"] = "";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String timezone_name = _storage->getTimezoneName();
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = timezone_name.isEmpty() ? "No timezone saved" : "Timezone saved";
    doc["timezone_name"] = timezone_name;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleTimezoneList() {
    size_t count = 0;
    const ClockSync::TimezoneInfo* timezones = ClockSync::getAvailableTimezones(count);
    JsonDocument doc;
    doc["success"] = true;
    JsonArray list = doc["timezones"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
        JsonObject item = list.add<JsonObject>();
        item["name"] = timezones[i].name;
        item["description"] = timezones[i].description;
    }

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleTimezoneSave() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String timezone_name = _server.arg("timezone_name");
    if (timezone_name.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err && doc["timezone_name"].is<String>()) {
                timezone_name = doc["timezone_name"].as<String>();
            }
        }
    }

    timezone_name.trim();
    if (timezone_name.isEmpty()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "timezone_name required";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    bool saved = _storage->setTimezoneName(timezone_name.c_str());
    JsonDocument doc;
    doc["success"] = saved;
    doc["message"] = saved ? "Timezone saved" : "Failed to save timezone";
    doc["timezone_name"] = saved ? timezone_name : "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(saved ? 200 : 500, response);
}

void CaptivePortal::handleTimezoneClear() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    bool cleared = _storage->setTimezoneName("");
    JsonDocument doc;
    doc["success"] = cleared;
    doc["message"] = cleared ? "Timezone cleared" : "Failed to clear timezone";
    doc["timezone_name"] = "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(cleared ? 200 : 500, response);
}

void CaptivePortal::handleLocationStatus() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        doc["location"] = "";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String location = _storage->getLocation();
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = location.isEmpty() ? "No location saved" : "Location saved";
    doc["location"] = location;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleLocationSave() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String location = _server.arg("location");
    if (location.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err && doc["location"].is<String>()) {
                location = doc["location"].as<String>();
            }
        }
    }

    location.trim();
    if (location.isEmpty()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "location required";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    bool saved = _storage->setLocation(location.c_str());
    JsonDocument doc;
    doc["success"] = saved;
    doc["message"] = saved ? "Location saved" : "Failed to save location";
    doc["location"] = saved ? location : "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(saved ? 200 : 500, response);
}

void CaptivePortal::handleLocationClear() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    bool cleared = _storage->setLocation("");
    JsonDocument doc;
    doc["success"] = cleared;
    doc["message"] = cleared ? "Location cleared" : "Failed to clear location";
    doc["location"] = "";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(cleared ? 200 : 500, response);
}

void CaptivePortal::handleClockStatus() {
    if (!_clock_controller) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Clock controller not ready";
        doc["enabled"] = false;
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Clock status loaded";
    doc["enabled"] = _clock_controller->isShowingClock();
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleClockApply() {
    if (!_clock_controller) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Clock controller not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String enabled_value = _server.arg("enabled");
    String timezone_name = _server.arg("timezone_name");
    if (enabled_value.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err) {
                if (enabled_value.isEmpty() && doc["enabled"].is<bool>()) {
                    enabled_value = doc["enabled"].as<bool>() ? "true" : "false";
                } else if (enabled_value.isEmpty() && doc["enabled"].is<String>()) {
                    enabled_value = doc["enabled"].as<String>();
                }
                if (timezone_name.isEmpty() && doc["timezone_name"].is<String>()) {
                    timezone_name = doc["timezone_name"].as<String>();
                }
            }
        }
    }

    enabled_value.trim();
    timezone_name.trim();

    bool enable_clock = enabled_value.equalsIgnoreCase("true") ||
        enabled_value.equalsIgnoreCase("1") ||
        enabled_value.equalsIgnoreCase("yes");

    if (enable_clock) {
        if (timezone_name.isEmpty() && _storage) {
            timezone_name = _storage->getTimezoneName();
            timezone_name.trim();
        }

        if (timezone_name.isEmpty()) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "timezone_name required to enable clock";
            String response;
            serializeJson(doc, response);
            sendJsonResponse(400, response);
            return;
        }

        ClockSync clock_sync;
        if (!clock_sync.setTimezoneByName(timezone_name.c_str())) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Invalid timezone_name";
            String response;
            serializeJson(doc, response);
            sendJsonResponse(400, response);
            return;
        }
        clock_sync.syncNow(nullptr, nullptr, 2000);

        bool ok = _clock_controller->showClock(timezone_name);
        JsonDocument doc;
        doc["success"] = ok;
        doc["message"] = ok ? "Clock enabled" : "Failed to enable clock";
        doc["enabled"] = ok;
        String response;
        serializeJson(doc, response);
        sendJsonResponse(ok ? 200 : 500, response);
        return;
    }

    _clock_controller->clearClock();
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Clock disabled";
    doc["enabled"] = false;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleAudioStatus() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        doc["enabled"] = true;
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    bool enabled = _storage->getAudioEnabled();
    uint8_t volume = _storage->getVolume();
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = enabled ? "Audio enabled" : "Audio muted";
    doc["enabled"] = enabled;
    doc["volume"] = volume;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleAudioApply() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String disabled_value = _server.arg("disabled");
    String volume_value = _server.arg("volume");
    if (disabled_value.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err) {
                if (doc["disabled"].is<bool>()) {
                    disabled_value = doc["disabled"].as<bool>() ? "true" : "false";
                } else if (doc["disabled"].is<String>()) {
                    disabled_value = doc["disabled"].as<String>();
                }
                if (volume_value.isEmpty() && doc["volume"].is<int>()) {
                    volume_value = String(doc["volume"].as<int>());
                } else if (volume_value.isEmpty() && doc["volume"].is<String>()) {
                    volume_value = doc["volume"].as<String>();
                }
            }
        }
    }

    disabled_value.trim();
    if (disabled_value.isEmpty()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "disabled required";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    int volume = -1;
    if (!volume_value.isEmpty()) {
        volume = constrain(volume_value.toInt(), 0, 100);
    }

    bool disabled = disabled_value.equalsIgnoreCase("true") ||
        disabled_value.equalsIgnoreCase("1") ||
        disabled_value.equalsIgnoreCase("yes");
    bool enabled = !disabled;

    _storage->setAudioEnabled(enabled);
    if (volume >= 0) {
        _storage->setVolume(static_cast<uint8_t>(volume));
    }
    if (_audio_codec) {
        _audio_codec->setMuted(!enabled);
        if (volume >= 0) {
            _audio_codec->setOutputVolume(volume);
        }
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = enabled ? "Audio enabled" : "Audio muted";
    doc["enabled"] = enabled;
    if (volume >= 0) {
        doc["volume"] = volume;
    } else {
        doc["volume"] = _storage->getVolume();
    }
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleAudioReset() {
    if (!_storage) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Storage not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    _storage->setVolume(70);
    _storage->setAudioEnabled(true);
    if (_audio_codec) {
        _audio_codec->setOutputVolume(70);
        _audio_codec->setMuted(false);
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Audio settings reset to defaults";
    doc["enabled"] = true;
    doc["volume"] = 70;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleEffectsStatus() {
    if (!_effects_manager) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Effects manager not ready";
        doc["effect"] = "none";
        doc["tint"] = "none";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    bool scanlines = _effects_manager->isScanlinesEnabled();
    bool dot_matrix = _effects_manager->isDotMatrixEnabled();
    bool glitch = _effects_manager->isGlitchEnabled();

    int enabled_count = (scanlines ? 1 : 0) + (dot_matrix ? 1 : 0) + (glitch ? 1 : 0);
    String effect = "none";
    if (enabled_count > 1) {
        effect = "multiple";
    } else if (scanlines) {
        effect = "scanlines";
    } else if (dot_matrix) {
        effect = "dot_matrix";
    } else if (glitch) {
        effect = "glitch";
    }

    String tint = "none";
    if (_effects_manager->isTintEnabled()) {
        RetroEffects::TintParams tint_params = _effects_manager->getTintParams();
        if (tint_params.tint_color == RetroTints::TINT_GREEN_400 ||
            tint_params.tint_color == RetroTints::TINT_GREEN_500) {
            tint = "green";
        } else if (tint_params.tint_color == RetroTints::TINT_BLUE_400 ||
                   tint_params.tint_color == RetroTints::TINT_BLUE_500) {
            tint = "blue";
        } else if (tint_params.tint_color == RetroTints::TINT_YELLOW_400 ||
                   tint_params.tint_color == RetroTints::TINT_YELLOW_500) {
            tint = "yellow";
        } else {
            tint = "custom";
        }
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Effects status loaded";
    doc["effect"] = effect;
    doc["tint"] = tint;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void CaptivePortal::handleEffectsApply() {
    if (!_effects_manager) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Effects manager not ready";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(500, response);
        return;
    }

    String effect = _server.arg("effect");
    String tint = _server.arg("tint");
    if (effect.isEmpty() || tint.isEmpty()) {
        String body = _server.arg("plain");
        if (!body.isEmpty()) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (!err) {
                if (effect.isEmpty() && doc["effect"].is<String>()) {
                    effect = doc["effect"].as<String>();
                }
                if (tint.isEmpty() && doc["tint"].is<String>()) {
                    tint = doc["tint"].as<String>();
                }
            }
        }
    }

    effect.trim();
    tint.trim();
    if (effect.isEmpty() && tint.isEmpty()) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "effect or tint required";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    bool ok = true;
    if (!effect.isEmpty()) {
        if (effect == "none") {
            _effects_manager->setScanlinesEnabled(false);
            _effects_manager->setDotMatrixEnabled(false);
            _effects_manager->setGlitchEnabled(false);
            if (_storage) {
                _storage->setEffectsScanlinesEnabled(false);
                _storage->setEffectsDotMatrixEnabled(false);
                _storage->setEffectsGlitchEnabled(false);
            }
        } else if (effect == "scanlines") {
            _effects_manager->setScanlinesEnabled(true);
            _effects_manager->setDotMatrixEnabled(false);
            _effects_manager->setGlitchEnabled(false);
            if (_storage) {
                _storage->setEffectsScanlinesEnabled(true);
                _storage->setEffectsDotMatrixEnabled(false);
                _storage->setEffectsGlitchEnabled(false);
            }
        } else if (effect == "dot_matrix") {
            _effects_manager->setScanlinesEnabled(false);
            _effects_manager->setDotMatrixEnabled(true);
            _effects_manager->setGlitchEnabled(false);
            if (_storage) {
                _storage->setEffectsScanlinesEnabled(false);
                _storage->setEffectsDotMatrixEnabled(true);
                _storage->setEffectsGlitchEnabled(false);
            }
        } else if (effect == "glitch") {
            _effects_manager->setScanlinesEnabled(false);
            _effects_manager->setDotMatrixEnabled(false);
            _effects_manager->setGlitchEnabled(true);
            if (_storage) {
                _storage->setEffectsScanlinesEnabled(false);
                _storage->setEffectsDotMatrixEnabled(false);
                _storage->setEffectsGlitchEnabled(true);
            }
        } else {
            ok = false;
        }
    }

    if (!tint.isEmpty()) {
        if (tint == "none") {
            _effects_manager->setTintEnabled(false);
            if (_storage) {
                _storage->setEffectsTintEnabled(false);
            }
        } else if (tint == "green" || tint == "blue" || tint == "yellow") {
            RetroEffects::TintParams tint_params = _effects_manager->getTintParams();
            if (tint == "green") {
                tint_params.tint_color = RetroTints::TINT_GREEN_400;
            } else if (tint == "blue") {
                tint_params.tint_color = RetroTints::TINT_BLUE_400;
            } else {
                tint_params.tint_color = RetroTints::TINT_YELLOW_400;
            }
            _effects_manager->setTintParams(tint_params);
            _effects_manager->setTintEnabled(true);
            if (_storage) {
                _storage->setEffectsTintColor(tint_params.tint_color);
                _storage->setEffectsTintEnabled(true);
            }
        } else {
            ok = false;
        }
    }

    if (!ok) {
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Invalid effect or tint selection";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(400, response);
        return;
    }

    handleEffectsStatus();
}

void CaptivePortal::sendJsonResponse(int code, const String& body) {
    _server.send(code, "application/json", body);
}

String CaptivePortal::getContentType(const String& path) const {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".gif")) return "image/gif";
    return "text/plain";
}

String CaptivePortal::authModeToString(wifi_auth_mode_t mode) const {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2 Enterprise";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "Unknown";
    }
}

String CaptivePortal::getSignalStrength(int rssi) const {
    if (rssi == 0) return "NOT_CONNECTED";
    if (rssi >= -30) return "Excellent";
    if (rssi >= -50) return "Good";
    if (rssi >= -70) return "Fair";
    if (rssi >= -90) return "Poor";
    return "Very Poor";
}

String CaptivePortal::buildJsonResponse(bool success,
                                        const String& message,
                                        const String& ssid,
                                        int rssi,
                                        bool connected,
                                        const JsonArray& networks) {
    JsonDocument doc;
    doc["success"] = success;
    doc["message"] = message;
    doc["ssid"] = ssid;
    doc["rssi"] = rssi;
    doc["signal_strength"] = getSignalStrength(rssi);
    doc["connected"] = connected;
    doc["state"] = "UPDATE_MODE";
    doc["system_mode"] = "Update Mode";
    doc["networks"] = networks;

    String response;
    serializeJson(doc, response);
    return response;
}
