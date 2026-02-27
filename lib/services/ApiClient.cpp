/**
 * ApiClient.cpp
 *
 * Implementation for ApiClient.
 */

#include "ApiClient.h"
#include "DeviceConfig.h"
#include "NvsStorage.h"
#include "ProtocolClient.h"

#include <esp_chip_info.h>
#include <esp_hmac.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <WiFi.h>

static const char* TAG = "ApiClient";

ApiClient::ApiClient(
    ProtocolClient* protocol,
    NVSStorage* storage
)
    : _protocol(protocol)
    , _storage(storage) {
}

bool ApiClient::sendJson(const JsonDocument& doc) {
    if (!_protocol || !_protocol->isConnected()) {
        ESP_LOGW(TAG, "Cannot send message: protocol not ready");
        return false;
    }

    String message;
    serializeJson(doc, message);
    bool sent = _protocol->sendMessage(message);
    if (sent) {
        ESP_LOGD(TAG, "📤 TX: %s", message.c_str());
    }
    return sent;
}

bool ApiClient::sendListenStart(const String& sessionId, bool autoMode) {
    if (!_protocol->isAudioChannelOpened()) {
        ESP_LOGW(TAG, "Cannot send listen start: audio channel not open");
        return false;
    }

    JsonDocument doc;
    doc["type"] = "listen";
    doc["session_id"] = sessionId;
    doc["state"] = "start";
    doc["mode"] = autoMode ? "auto" : "manual";

    ESP_LOGI(TAG, "Protocol: [Listen] start session_id=%s mode=%s",
             sessionId.c_str(), autoMode ? "auto" : "manual");
    bool sent = sendJson(doc);
    ESP_LOGI(TAG, "Protocol: [Listen] publish %s", sent ? "ok" : "failed");
    return sent;
}

bool ApiClient::sendListenDetect(const String& sessionId, const String& text) {
    if (!_protocol || !_protocol->isConnected()) {
        ESP_LOGW(TAG, "Cannot send listen detect: protocol not ready");
        return false;
    }

    JsonDocument doc;
    doc["type"] = "listen";
    doc["session_id"] = sessionId;
    doc["state"] = "detect";
    doc["text"] = text;

    ESP_LOGI(TAG, "Protocol: [Listen] detect session_id=%s text=%s",
             sessionId.c_str(), text.c_str());
    bool sent = sendJson(doc);
    ESP_LOGI(TAG, "Protocol: [Listen] publish %s", sent ? "ok" : "failed");
    return sent;
}

bool ApiClient::sendListenStop(const String& sessionId) {
    if (!_protocol->isConnected()) {
        return false;
    }

    JsonDocument doc;
    doc["type"] = "listen";
    doc["session_id"] = sessionId;
    doc["state"] = "stop";

    ESP_LOGI(TAG, "Protocol: [Listen] stop session_id=%s", sessionId.c_str());
    bool sent = sendJson(doc);
    ESP_LOGI(TAG, "Protocol: [Listen] publish %s", sent ? "ok" : "failed");
    return sent;
}

bool ApiClient::sendAbort(const String& sessionId, const String& reason) {
    if (!_protocol->isConnected()) {
        return false;
    }

    JsonDocument doc;
    doc["type"] = "abort";
    doc["session_id"] = sessionId;
    doc["reason"] = reason;

    ESP_LOGI(TAG, "Protocol: [Listen] abort session_id=%s reason=%s",
             sessionId.c_str(), reason.c_str());
    bool sent = sendJson(doc);
    ESP_LOGI(TAG, "Protocol: [Listen] publish %s", sent ? "ok" : "failed");
    return sent;
}

bool ApiClient::sendMcpResponse(const String& sessionId, const JsonObject& payload) {
    if (!_protocol->isConnected()) {
        return false;
    }

    JsonDocument doc;
    doc["type"] = "mcp";
    doc["session_id"] = sessionId;
    doc["payload"] = payload;

    String preview;
    serializeJson(doc, preview);
    ESP_LOGI(TAG, "MCP response publish: %s", preview.c_str());
    bool sent = sendJson(doc);
    ESP_LOGI(TAG, "Protocol: [MCP TX] publish %s", sent ? "ok" : "failed");
    return sent;
}

String ApiClient::generateActivationPayload(const String& serialNumber, const String& challenge) {
    JsonDocument doc;

    // V2 Activation (with serial number and HMAC challenge)
    if (!serialNumber.isEmpty() && !challenge.isEmpty()) {
        ESP_LOGI(TAG, "Building V2 activation payload");

        uint8_t hmac_result[32];
        esp_err_t err = esp_hmac_calculate(
            HMAC_KEY0,
            (const uint8_t*)challenge.c_str(),
            challenge.length(),
            hmac_result
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calculate HMAC: %d", err);
            return "{}";
        }

        char hmac_hex[65];
        for (int i = 0; i < 32; i++) {
            snprintf(hmac_hex + i * 2, sizeof(hmac_hex) - i * 2, "%02x", hmac_result[i]);
        }

        doc["algorithm"] = "hmac-sha256";
        doc["serial_number"] = serialNumber;
        doc["challenge"] = challenge;
        doc["hmac"] = String(hmac_hex);
    } else {
        // V1 Activation (no serial number)
        ESP_LOGI(TAG, "Building V1 activation payload");
        doc.to<JsonObject>();
    }

    String payload;
    serializeJson(doc, payload);
    return payload;
}

String ApiClient::buildDeviceInfo(NVSStorage* storage)
{
    // Build device info JSON
    JsonDocument doc;
    doc["version"] = 2;
    doc["language"] = DEFAULT_LANGUAGE;
    doc["flash_size"] = ESP.getFlashChipSize();
    doc["psram_size"] = ESP.getPsramSize();
    doc["minimum_free_heap_size"] = ESP.getMinFreeHeap();

    // Get MAC address
    String mac_addr = WiFi.macAddress();
    mac_addr.toLowerCase();
    doc["mac_address"] = mac_addr;

    // Get UUID from storage
    String uuid = storage->getDeviceUUID();
    doc["uuid"] = uuid;
    doc["chip_model_name"] = ESP.getChipModel();

    // Chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    JsonObject chip_info_obj = doc["chip_info"].to<JsonObject>();
    chip_info_obj["model"] = (int)chip_info.model;
    chip_info_obj["cores"] = chip_info.cores;
    chip_info_obj["revision"] = chip_info.revision;
    chip_info_obj["features"] = chip_info.features;

    // Application info
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc_data;
    JsonObject app = doc["application"].to<JsonObject>();

    if (running) {
        esp_err_t err = esp_ota_get_partition_description(running, &app_desc_data);
        if (err == ESP_OK) {
            app["name"] = app_desc_data.project_name;
            app["version"] = app_desc_data.version;
            app["compile_time"] = String(app_desc_data.date) + "T" + String(app_desc_data.time) + "Z";
            app["idf_version"] = app_desc_data.idf_ver;

            // Convert SHA256 to hex string
            char sha256_str[65];
            for (int i = 0; i < 32; i++) {
                snprintf(sha256_str + i * 2, sizeof(sha256_str) - i * 2, "%02x",
                         app_desc_data.app_elf_sha256[i]);
            }
            app["elf_sha256"] = String(sha256_str);
        } else {
            ESP_LOGW(TAG, "Failed to get app description, using defaults");
            app["name"] = "xiaozhi-esp32";
            app["version"] = "2.0.0";
            app["compile_time"] = String(__DATE__) + "T" + String(__TIME__) + "Z";
            app["idf_version"] = ESP.getSdkVersion();
            app["elf_sha256"] = "";
        }
    }

    // Partition table
    JsonArray partition_table = doc["partition_table"].to<JsonArray>();
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t* partition = esp_partition_get(it);
        JsonObject p = partition_table.add<JsonObject>();
        p["label"] = partition->label;
        p["type"] = partition->type;
        p["subtype"] = partition->subtype;
        p["address"] = partition->address;
        p["size"] = partition->size;
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    // OTA partition info
    JsonObject ota = doc["ota"].to<JsonObject>();
    if (running) {
        ota["label"] = running->label;
    }

    // Display info (SSD1351 128x128 color display)
    JsonObject display = doc["display"].to<JsonObject>();
    display["monochrome"] = false;
    display["width"] = DISPLAY_WIDTH;
    display["height"] = DISPLAY_HEIGHT;

    // Board info
    JsonObject board = doc["board"].to<JsonObject>();
    String board_type = String(getBoardType());
    board["type"] = board_type;
    board["name"] = String(getBoardType());
    board["ssid"] = WiFi.SSID();
    board["rssi"] = WiFi.RSSI();
    board["channel"] = WiFi.channel();
    board["ip"] = WiFi.localIP().toString();
    board["mac"] = mac_addr;
    doc["firmware_version"] = FIRMWARE_VERSION;

    String device_info;
    serializeJson(doc, device_info);
    return device_info;
}
