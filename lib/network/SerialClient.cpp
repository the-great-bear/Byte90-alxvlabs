/**
 * SerialClient.cpp
 *
 * Implementation for SerialClient.
 */

#include "SerialClient.h"
#include "NvsStorage.h"
#include "TimerManager.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_ota_ops.h>

static const char* TAG = "SerialClient";
static const uint8_t BASE64_DECODE_TABLE[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62,  255,
    255, 255, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  255, 255,
    255, 254, 255, 255, 255, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
    10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
    25,  255, 255, 255, 255, 255, 255, 26,  27,  28,  29,  30,  31,  32,  33,
    34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255};

SerialClient::SerialClient(WifiManager* wifiClient, NVSStorage* storage)
    : _wifiClient(wifiClient),
      _storage(storage),
      _verbose(false),
      _update_state(SerialUpdateState::IDLE),
      _update_progress{0, 0, 0, ""},
      _expected_firmware_size(0),
      _current_update_command(U_FLASH),
      _total_written(0) {
}

bool SerialClient::begin() {
    Serial.setRxBufferSize(8192);
    Serial.setTxBufferSize(4096);
    Serial.begin(SERIAL_BAUD_RATE);
    
    delay(100);
    
    ESP_LOGI(TAG, "Serial WiFi configuration interface ready");
    
    _update_state = SerialUpdateState::IDLE;
    _update_progress = {0, 0, 0, ""};
    _expected_firmware_size = 0;
    _current_update_command = U_FLASH;
    _total_written = 0;

    // Send ready message
    String readyMsg = createSerialUpdateResponse(
        true, "BYTE-90 Serial Interface Ready - WiFi commands available");
    sendResponse(readyMsg);
    
    return true;
}

void SerialClient::loop() {
    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            if (_commandBuffer.length() > 0) {
                processCommand(_commandBuffer);
                _commandBuffer = "";
            }
        } else if (c != '\r') {
            _commandBuffer += c;
            
            // Prevent buffer overflow
            if (_commandBuffer.length() > SERIAL_COMMAND_BUFFER_SIZE) {
                String response = createSerialUpdateResponse(false, "Command too long");
                sendResponse(response, true);
                _commandBuffer = "";
            }
        }
    }
}

void SerialClient::processCommand(const String& line) {
    int colonPos = line.indexOf(':');
    String command, data;
    
    if (colonPos != -1) {
        command = line.substring(0, colonPos);
        data = line.substring(colonPos + 1);
        command.trim();
        data.trim();
    } else {
        command = line;
        command.trim();
    }
    
    if (_verbose) {
        ESP_LOGI(TAG, "Command: %s", command.c_str());
    }
    
    // Firmware update commands
    if (command == CMD_START_UPDATE) {
        handleStartUpdate(data);
    } else if (command == CMD_SEND_CHUNK) {
        handleSendChunk(data);
    } else if (command == CMD_FINISH_UPDATE) {
        handleFinishUpdate();
    } else if (command == CMD_ABORT_UPDATE) {
        handleAbortUpdate();
    }
    // Handle commands
    else if (command == CMD_WIFI_SCAN) {
        handleWiFiScan();
    } else if (command == CMD_WIFI_STATUS) {
        handleWiFiStatus();
    } else if (command == CMD_WIFI_CONNECT) {
        handleWiFiConnect(data);
    } else if (command == CMD_WIFI_DISCONNECT) {
        handleWiFiDisconnect();
    } else if (command == CMD_WIFI_GET_SAVED) {
        handleWiFiGetSaved();
    } else if (command == CMD_WIFI_FORGET) {
        handleWiFiForget();
    } else if (command == CMD_GET_INFO) {
        handleGetInfo();
    } else if (command == CMD_GET_STATUS) {
        handleGetStatus();
    } else if (command == CMD_RESTART) {
        handleRestart();
    } else if (command == CMD_GET_LOGS) {
        String response = createSerialUpdateResponse(
            true, "Verbose logging " + String(_verbose ? "enabled" : "disabled"));
        sendResponse(response);
    } else if (command == CMD_VERBOSE) {
        _verbose = (data == "1" || data.equalsIgnoreCase("true"));
        String response = createSerialUpdateResponse(
            true, "Verbose logging " + String(_verbose ? "enabled" : "disabled"));
        sendResponse(response);
    } else {
        String response = createSerialUpdateResponse(false, "Unknown command: " + command);
        sendResponse(response, true);
    }
}

void SerialClient::sendResponse(const String& jsonResponse, bool isError) {
    Serial.println((isError ? RESP_ERROR : RESP_OK) + jsonResponse);
}

// JSON response formatters
String SerialClient::createJsonResponse(bool success, const String& message,
                                           const String& ssid, int rssi,
                                           bool connected, const String& networks) {
    JsonDocument doc;
    
    doc["success"] = success;
    doc["message"] = message;
    doc["ssid"] = ssid;
    doc["rssi"] = rssi;
    doc["signal_strength"] = getSignalStrength(rssi);
    doc["connected"] = connected;
    doc["state"] = "UPDATE_MODE";  // <-- Add this
    doc["system_mode"] = "Update Mode";  // <-- Add this
    
    // Parse networks JSON if provided
    if (networks != "[]") {
        JsonDocument networksDoc;
        deserializeJson(networksDoc, networks);
        doc["networks"] = networksDoc.as<JsonArray>();
    } else {
        doc["networks"] = JsonArray();
    }
    
    String response;
    serializeJson(doc, response);
    return response;
}

String SerialClient::createDeviceInfoResponse() {
    JsonDocument doc;
    
    doc["success"] = true;
    doc["message"] = "Device information";
    doc["current_mode"] = "Update Mode"; 
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["mcu"] = ESP.getChipModel();
    doc["chip_revision"] = String(ESP.getChipRevision());
    doc["flash_size"] = String(ESP.getFlashChipSize() / 1024) + "KB";
    doc["free_heap"] = String(ESP.getFreeHeap() / 1024) + "KB";
    
    // OTA partition info
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        doc["running_partition"] = running->label;
    }
    
    String response;
    serializeJson(doc, response);
    return response;
}

String SerialClient::createWiFiCredentialsResponse(bool success, const String& message,
                                                       const String& ssid, bool hasCredentials) {
    JsonDocument doc;
    
    doc["success"] = success;
    doc["message"] = message;
    doc["ssid"] = ssid;
    doc["has_credentials"] = hasCredentials;
    
    String response;
    serializeJson(doc, response);
    return response;
}

String SerialClient::createSerialUpdateResponse(bool success, const String& message,
                                               bool completed, int progress) {
    const char* state = getSerialStateString();
    size_t estimatedSize = 150 + message.length() + strlen(state) + strlen(FIRMWARE_VERSION);

    String response;
    response.reserve(estimatedSize);

    response += "{\"success\":";
    response += success ? "true" : "false";
    response += ",\"state\":\"";
    response += state;
    response += "\",\"progress\":";
    response += progress;
    response += ",\"received\":";
    response += _update_progress.receivedSize;
    response += ",\"total\":";
    response += _update_progress.totalSize;
    response += ",\"version\":\"";
    response += FIRMWARE_VERSION;
    response += "\",\"message\":\"";
    response += message;
    response += "\",\"completed\":";
    response += completed ? "true" : "false";
    response += "}";

    return response;
}

void SerialClient::sendProgressUpdate(int percentage, const String& message) {
    String response = createSerialUpdateResponse(
        (_update_state != SerialUpdateState::ERROR), message,
        (_update_state == SerialUpdateState::SUCCESS), percentage);
    Serial.println(RESP_PROGRESS + response);
}

size_t SerialClient::simpleBase64Decode(const String& input, uint8_t* output, size_t maxOutputSize) {
    if (input.length() % 4 != 0) {
        return 0;
    }

    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) {
            return 0;
        }
    }

    size_t outputLen = 0;
    size_t inputLen = input.length();

    for (size_t i = 0; i < inputLen && outputLen < maxOutputSize - 3; i += 4) {
        uint8_t a = BASE64_DECODE_TABLE[static_cast<uint8_t>(input[i])];
        uint8_t b = BASE64_DECODE_TABLE[static_cast<uint8_t>(input[i + 1])];
        uint8_t c = BASE64_DECODE_TABLE[static_cast<uint8_t>(input[i + 2])];
        uint8_t d = BASE64_DECODE_TABLE[static_cast<uint8_t>(input[i + 3])];

        if (a == 255 || b == 255) {
            return 0;
        }

        output[outputLen++] = (a << 2) | (b >> 4);

        if (c != 254 && outputLen < maxOutputSize) {
            output[outputLen++] = (b << 4) | (c >> 2);
        }

        if (d != 254 && outputLen < maxOutputSize) {
            output[outputLen++] = (c << 6) | d;
        }
    }

    return outputLen;
}

const char* SerialClient::getSerialStateString() const {
    switch (_update_state) {
        case SerialUpdateState::IDLE:
            return "IDLE";
        case SerialUpdateState::RECEIVING:
            return "RECEIVING";
        case SerialUpdateState::PROCESSING:
            return "PROCESSING";
        case SerialUpdateState::SUCCESS:
            return "SUCCESS";
        case SerialUpdateState::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

bool SerialClient::initializeSerialUpdate(const String& data) {
    int commaPos = data.indexOf(',');
    if (commaPos == -1) {
        String response = createSerialUpdateResponse(
            false, "Invalid START_UPDATE format. Expected: size,type");
        sendResponse(response, true);
        return false;
    }

    String sizeStr = data.substring(0, commaPos);
    String typeStr = data.substring(commaPos + 1);

    _expected_firmware_size = sizeStr.toInt();

    size_t maxAllowedSize = 0;
    const esp_partition_t* update_partition = nullptr;
    if (typeStr == "filesystem") {
        _current_update_command = U_SPIFFS;
        update_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        if (update_partition) {
            maxAllowedSize = update_partition->size;
        }
    } else if (typeStr == "firmware") {
        _current_update_command = U_FLASH;
        update_partition = esp_ota_get_next_update_partition(nullptr);
        if (update_partition) {
            maxAllowedSize = update_partition->size;
        }
    } else {
        String response = createSerialUpdateResponse(
            false, "Invalid update type. Expected: firmware or filesystem");
        sendResponse(response, true);
        return false;
    }

    if (!update_partition || maxAllowedSize == 0) {
        String response = createSerialUpdateResponse(
            false, "Update partition not available");
        sendResponse(response, true);
        return false;
    }

    if (_expected_firmware_size == 0) {
        String response = createSerialUpdateResponse(false, "Invalid file size");
        sendResponse(response, true);
        return false;
    }

    if (_expected_firmware_size < 1024 || _expected_firmware_size > maxAllowedSize) {
        String response = createSerialUpdateResponse(
            false, "File size out of range (1KB - " + String(maxAllowedSize) + " bytes)");
        sendResponse(response, true);
        return false;
    }

    if (!Update.begin(_expected_firmware_size, _current_update_command)) {
        String response = createSerialUpdateResponse(
            false, "Failed to initialize update: " + String(Update.errorString()));
        sendResponse(response, true);
        return false;
    }

    return true;
}

int SerialClient::handleChunkWrite(const String& data) {
    static uint8_t decodedBuffer[2048];
    size_t decodedSize = simpleBase64Decode(data, decodedBuffer, sizeof(decodedBuffer));

    if (decodedSize == 0) {
        Serial.println(String(RESP_ERROR) + "{\"success\":false,\"message\":\"Decode failed\"}");
        return -1;
    }

    if (_total_written + decodedSize > _expected_firmware_size) {
        _update_state = SerialUpdateState::ERROR;
        Update.abort();
        Serial.println(String(RESP_ERROR) + "{\"success\":false,\"message\":\"Data exceeds expected size\"}");
        return -1;
    }

    size_t written = Update.write(decodedBuffer, decodedSize);
    if (written != decodedSize) {
        _update_state = SerialUpdateState::ERROR;
        Update.abort();
        Serial.println(String(RESP_ERROR) + "{\"success\":false,\"message\":\"Flash write failed\"}");
        return -1;
    }

    _total_written += written;
    _update_progress.receivedSize += written;
    _update_progress.percentage =
        (_update_progress.receivedSize * 100) / _update_progress.totalSize;

    return _update_progress.percentage;
}

String SerialClient::getSecurityType(wifi_auth_mode_t encType) {
    switch (encType) {
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

String SerialClient::getSignalStrength(int rssi) {
    if (rssi == 0) return "NOT_CONNECTED";
    else if (rssi >= -30) return "Excellent";
    else if (rssi >= -50) return "Good";
    else if (rssi >= -70) return "Fair";
    else if (rssi >= -90) return "Poor";
    else return "Very Poor";
}

// Command handlers implementation

void SerialClient::handleWiFiScan() {
    if (isSerialUpdateActive()) {
        String response = createSerialUpdateResponse(
            false, "Cannot scan WiFi during firmware update");
        sendResponse(response, true);
        return;
    }
    ESP_LOGI(TAG, "Scanning networks...");
    
    int networkCount = _wifiClient->scanNetworks();
    
    if (networkCount < 0) {
        String response = createJsonResponse(false, "Scan failed");
        sendResponse(response, true);
        return;
    }
    
    // Build networks JSON array
    JsonDocument doc;
    JsonArray networksArray = doc.to<JsonArray>();
    
    for (int i = 0; i < networkCount; i++) {
        JsonObject network = networksArray.add<JsonObject>();
        
        String ssid = _wifiClient->getScannedSSID(i);
        int rssi = _wifiClient->getScannedRSSI(i);
        wifi_auth_mode_t encType = _wifiClient->getScannedEncryption(i);
        
        network["ssid"] = ssid;
        network["rssi"] = rssi;
        network["signal_strength"] = getSignalStrength(rssi);
        network["encryption_type"] = (int)encType;
        network["is_open"] = (encType == WIFI_AUTH_OPEN);
        network["security"] = getSecurityType(encType);
    }
    
    String networksJson;
    serializeJson(networksArray, networksJson);
    
    _wifiClient->clearScan();
    
    String message = "Found " + String(networkCount) + " networks";
    String response = createJsonResponse(true, message, "", 0, 
                                        _wifiClient->isConnected(), networksJson);
    sendResponse(response);
}

void SerialClient::handleWiFiStatus() {
    bool connected = _wifiClient->isConnected();
    String ssid = connected ? _wifiClient->getSSID() : "";
    int rssi = connected ? _wifiClient->getRSSI() : 0;
    String message = connected ? "Connected to " + ssid : "Not connected";
    
    String response = createJsonResponse(true, message, ssid, rssi, connected);
    sendResponse(response);
}

void SerialClient::handleWiFiConnect(const String& data) {
    if (isSerialUpdateActive()) {
        String response = createSerialUpdateResponse(
            false, "Cannot connect to WiFi during firmware update");
        sendResponse(response, true);
        return;
    }
    ESP_LOGI(TAG, "Connect command received: %s", data.c_str());
    
    int commaPos = data.indexOf(',');
    if (commaPos == -1) {
        String response = createJsonResponse(false, "Invalid format. Expected: ssid,password");
        sendResponse(response, true);
        return;
    }
    
    String ssid = data.substring(0, commaPos);
    String password = data.substring(commaPos + 1);
    ssid.trim();
    password.trim();
    
    if (ssid.isEmpty()) {
        String response = createJsonResponse(false, "SSID cannot be empty");
        sendResponse(response, true);
        return;
    }
    
    // Check if already connected
    if (_wifiClient->isConnected() && _wifiClient->getSSID().equals(ssid)) {
        // Save credentials
        if (_storage) {
            _storage->saveWiFiCredentials(ssid.c_str(), password.c_str());
        }
        
        String response = createJsonResponse(true, "Already connected to " + ssid,
                                            ssid, _wifiClient->getRSSI(), true);
        sendResponse(response);
        return;
    }
    
    // Disconnect if connected to different network
    if (_wifiClient->isConnected()) {
        _wifiClient->disconnect();
        delay(1000);
    }
    
    // Start connection
    if (!_wifiClient->connect(ssid.c_str(), password.c_str())) {
        String response = createJsonResponse(false, "Connection failed - invalid parameters");
        sendResponse(response, true);
        return;
    }
    
    // Wait for connection with timeout
    unsigned long startTime = millis();
    const unsigned long TIMEOUT_MS = 20000;  // 20 seconds
    
    while (millis() - startTime < TIMEOUT_MS) {
        delay(500);
        _wifiClient->loop();  // Process WiFi events
        
        if (_wifiClient->isConnected()) {
            ESP_LOGI(TAG, "Connected successfully");
            
            // Save credentials on success
            if (_storage) {
                _storage->saveWiFiCredentials(ssid.c_str(), password.c_str());
            }
            
            String response = createJsonResponse(true, "Connected to " + ssid,
                                                ssid, _wifiClient->getRSSI(), true);
            sendResponse(response);
            return;
        }
    }
    
    // Timeout
    ESP_LOGE(TAG, "Connection timeout");
    String response = createJsonResponse(false, "Connection timeout - verify password");
    sendResponse(response, true);
}

void SerialClient::handleWiFiDisconnect() {
    if (isSerialUpdateActive()) {
        String response = createSerialUpdateResponse(
            false, "Cannot disconnect WiFi during firmware update");
        sendResponse(response, true);
        return;
    }
    bool wasConnected = _wifiClient->isConnected();
    String currentSSID = _wifiClient->getSSID();
    
    _wifiClient->disconnect();
    
    String message = wasConnected ? "Disconnected from " + currentSSID 
                                  : "WiFi was already disconnected";
    String response = createJsonResponse(true, message);
    sendResponse(response);
}

void SerialClient::handleWiFiGetSaved() {
    if (!_storage) {
        String response = createWiFiCredentialsResponse(false, "Storage not available");
        sendResponse(response, true);
        return;
    }
    
    wifi_credentials_t credentials;
    bool hasCredentials = _storage->loadWiFiCredentials(&credentials);
    
    if (hasCredentials) {
        String message = "Saved credentials found for: " + String(credentials.ssid);
        String response = createWiFiCredentialsResponse(true, message, 
                                                       String(credentials.ssid), true);
        sendResponse(response);
    } else {
        String response = createWiFiCredentialsResponse(true, 
                                                       "No saved WiFi credentials", "", false);
        sendResponse(response);
    }
}

void SerialClient::handleWiFiForget() {
    if (isSerialUpdateActive()) {
        String response = createSerialUpdateResponse(
            false, "Cannot clear WiFi credentials during firmware update");
        sendResponse(response, true);
        return;
    }
    if (!_storage) {
        String response = createWiFiCredentialsResponse(false, "Storage not available");
        sendResponse(response, true);
        return;
    }
    
    wifi_credentials_t credentials;
    bool hadCredentials = _storage->loadWiFiCredentials(&credentials);
    
    _storage->clearWiFiCredentials();
    
    String message = hadCredentials ? "Cleared credentials for " + String(credentials.ssid)
                                    : "No credentials were saved";
    String response = createWiFiCredentialsResponse(true, message, "", false);
    sendResponse(response);
}

void SerialClient::handleGetInfo() {
    String response = createDeviceInfoResponse();
    sendResponse(response);
}

void SerialClient::handleGetStatus() {
    JsonDocument doc;
    
    doc["success"] = true;
    doc["message"] = "Device status";
    doc["state"] = "UPDATE_MODE";
    doc["system_mode"] = "Update Mode";
    doc["current_mode"] = "Update Mode"; 
    doc["update_active"] = (_update_state != SerialUpdateState::IDLE);
    doc["serial_state"] = getSerialStateString();
    doc["progress"] = _update_progress.percentage;
    doc["received"] = _update_progress.receivedSize;
    doc["total"] = _update_progress.totalSize;
    doc["wifi_connected"] = _wifiClient->isConnected();
    doc["free_heap"] = ESP.getFreeHeap();

    if (_wifiClient->isConnected()) {
        doc["ssid"] = _wifiClient->getSSID();
        doc["rssi"] = _wifiClient->getRSSI();
        doc["ip"] = _wifiClient->getIP();
    }

    // Active timers (F1). Reported here so the GET_STATUS poll can verify timer
    // state deterministically over serial without depending on log levels.
    JsonArray timers = doc["timers"].to<JsonArray>();
    if (_timer_manager) {
        uint64_t now_ms = millis();
        for (const auto& e : _timer_manager->listActive()) {
            uint32_t remaining = (e.end_ms > now_ms)
                ? static_cast<uint32_t>((e.end_ms - now_ms) / 1000ULL)
                : 0;
            JsonObject obj = timers.add<JsonObject>();
            obj["id"]                = e.id;
            obj["label"]             = e.label;
            obj["duration_seconds"]  = e.duration_seconds;
            obj["remaining_seconds"] = remaining;
            obj["ends_at_epoch_ms"]  = e.end_epoch_ms;
        }
    }
    doc["timer_count"] = timers.size();

    String response;
    serializeJson(doc, response);
    sendResponse(response);
}

void SerialClient::handleRestart() {
    String response = createJsonResponse(true, "Restarting device...");
    sendResponse(response);
    delay(1000);
    ESP.restart();
}

void SerialClient::handleStartUpdate(const String& data) {
    if (_update_state != SerialUpdateState::IDLE) {
        if (Update.isRunning()) {
            Update.abort();
        }
        _update_state = SerialUpdateState::IDLE;
        _update_progress = {0, 0, 0, ""};
    }

    if (!initializeSerialUpdate(data)) {
        return;
    }

    _update_progress.totalSize = _expected_firmware_size;
    _update_progress.receivedSize = 0;
    _update_progress.percentage = 0;
    _update_progress.message = "Update started";
    _total_written = 0;

    _update_state = SerialUpdateState::RECEIVING;

    String response = createSerialUpdateResponse(
        true, "Update initialized. Ready to receive data.");
    sendResponse(response);
    sendProgressUpdate(0, "Ready to receive firmware data");
}

void SerialClient::handleSendChunk(const String& data) {
    if (_update_state != SerialUpdateState::RECEIVING) {
        Serial.println(String(RESP_ERROR) + "{\"success\":false,\"message\":\"Not receiving\"}");
        return;
    }

    if (data.length() == 0) {
        Serial.println(String(RESP_ERROR) + "{\"success\":false,\"message\":\"Empty chunk\"}");
        return;
    }

    int progress = handleChunkWrite(data);
    if (progress < 0) {
        return;
    }

    Serial.println(String(RESP_OK) + "{\"success\":true}");

    static int lastPercent = -1;
    if (_update_progress.percentage >= lastPercent + 10) {
        lastPercent = _update_progress.percentage;
        sendProgressUpdate(_update_progress.percentage, "Uploading firmware...");
    }
}

void SerialClient::handleFinishUpdate() {
    if (_update_state != SerialUpdateState::RECEIVING) {
        String response = createSerialUpdateResponse(false, "Not in receiving state");
        sendResponse(response, true);
        return;
    }

    _update_state = SerialUpdateState::PROCESSING;
    sendProgressUpdate(100, "Finalizing update...");

    if (_total_written != _expected_firmware_size) {
        _update_state = SerialUpdateState::ERROR;
        Update.abort();
        String response = createSerialUpdateResponse(
            false, "Size mismatch - Expected: " + String(_expected_firmware_size) +
                       ", Received: " + String(_total_written));
        sendResponse(response, true);
        return;
    }

    if (Update.end(true)) {
        _update_state = SerialUpdateState::SUCCESS;
        _update_progress.message = "Update completed successfully";
        String response = createSerialUpdateResponse(
            true, "Update completed successfully. Device will restart.", true, 100);
        sendResponse(response);
        delay(1000);
        ESP.restart();
    } else {
        _update_state = SerialUpdateState::ERROR;
        String response = createSerialUpdateResponse(
            false, "Update failed: " + String(Update.errorString()));
        sendResponse(response, true);
        Update.abort();
    }
}

void SerialClient::handleAbortUpdate() {
    if (_update_state == SerialUpdateState::IDLE) {
        String response = createSerialUpdateResponse(true, "No update in progress");
        sendResponse(response);
        return;
    }

    Update.abort();
    _update_state = SerialUpdateState::IDLE;
    _update_progress = {0, 0, 0, "Update aborted"};

    String response = createSerialUpdateResponse(true, "Update aborted");
    sendResponse(response);
}
