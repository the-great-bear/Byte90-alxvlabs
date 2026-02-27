/**
 * McpToolRegistry.cpp
 *
 * Implementation for McpToolRegistry.
 */

#include "McpToolRegistry.h"
#include "ArduinoSSD1351.h"
#include "AudioCodec.h"
#include "ClockSync.h"
#include "DigitalClockController.h"
#include "AudioService.h"
#include "Axp2101.h"
#include "GifManager.h"
#include "ClockSync.h"
#include "McpServer.h"
#include "EffectsManager.h"
#include "RetroTints.h"
#include "DeviceConfig.h"
#include "NvsStorage.h"
#include "TimerManager.h"
#include "certs/GoogleMapsCert.h"
#include "SecureHttpClient.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

// Components are now accessed through McpServer instead of global functions

// Board name from device config
#ifndef BOARD_NAME
#define BOARD_NAME "ESP32-S3"
#endif

static const char* TAG = "McpToolRegistry";

namespace {
String urlEncode(const String& input) {
    String encoded;
    encoded.reserve(input.length() * 3);
    for (size_t i = 0; i < input.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

String urlEncodePreserveCommas(const String& input) {
    String encoded = urlEncode(input);
    encoded.replace("%2C", ",");
    return encoded;
}

} // namespace

void McpToolRegistry::buildDeviceToolRegistry(McpServer* mcp)
{
    ESP_LOGI(TAG, "Registering MCP tools...");

    registerStatusTools(mcp);
    registerAudioTools(mcp);
    registerDisplayTools(mcp);
    registerEffectsTools(mcp);
    registerTimerTools(mcp);

    ESP_LOGI(TAG, "✅ Device MCP tool registry built");
}

void McpToolRegistry::registerAudioTools(McpServer* mcp)
{
    // ========================================================================
    // AUDIO TOOLS
    // ========================================================================

    mcp->addTool(
        "self.audio_speaker.set_volume",
        "Set the volume of the audio speaker (0-100).\n"
        "Use this tool for:\n"
        "1. Changing speaker volume when the user asks to raise/lower volume.\n"
        "2. Applying a specific volume level after checking current status with self.get_device_status.",
        PropertyList({
            Property("volume", PROPERTY_TYPE_INTEGER, 0, 100)
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            int volume = params["volume"].getIntValue();

            // Access audio codec through McpServer
            auto* audio_codec = mcp->getAudioCodec();

            if (audio_codec != nullptr) {
                audio_codec->setOutputVolume(volume);
                ESP_LOGI(TAG, "[MCP] Volume set to: %d", volume);
                return ReturnValue(true);
            }

            ESP_LOGE(TAG, "[MCP] Audio codec not available");
            return ReturnValue(false);
        }
    );

}

void McpToolRegistry::registerDisplayTools(McpServer* mcp)
{
    // ========================================================================
    // DISPLAY TOOLS
    // ========================================================================

    mcp->addTool(
        "self.display.set_brightness",
        "Set the display brightness (0-100).\n"
        "Use this tool for:\n"
        "1. Adjusting screen brightness on user request.\n"
        "2. Applying a specific brightness after checking current status with self.get_device_status.",
        PropertyList({
            Property("brightness", PROPERTY_TYPE_INTEGER, 0, 100)
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            int brightness = params["brightness"].getIntValue();

            // Access display through McpServer
            auto* display = mcp->getDisplay();

            if (display != nullptr) {
                display->setBrightnessPercent(brightness);
                ESP_LOGI(TAG, "[MCP] Display brightness set to: %d", brightness);
                if (mcp->getStorage()) {
                    mcp->getStorage()->setBrightness(brightness);
                }
                return ReturnValue(true);
            }

            ESP_LOGE(TAG, "[MCP] Display not available");
            return ReturnValue(false);
        }
    );

    mcp->addTool(
        "self.display.show_clock",
        "Display a digital clock on screen using the provided timezone and read out the current time.\n"
        "Use this tool for:\n"
        "1. Entering clock mode when the user asks to show the clock/time on screen.\n"
        "2. Showing the time in a specific timezone (set or provided).\n"
        "Notes: call self.get_device_status first to confirm whether clock mode is already active.",
        PropertyList({
            Property("timezone_name", PROPERTY_TYPE_STRING, "")
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            String timezone_name = params["timezone_name"].getStringValue();
            timezone_name.trim();
            if (timezone_name.isEmpty() && mcp && mcp->getStorage()) {
                timezone_name = mcp->getStorage()->getTimezoneName();
                timezone_name.trim();
            }
            if (timezone_name.isEmpty()) {
                return ReturnValue("{\"error\":\"timezone_required\",\"message\":\"Provide timezone_name (e.g., North_America_Eastern, UTC, China) or set it with self.set_timezone.\"}");
            }

            ClockSync clock_sync;
            if (!clock_sync.setTimezoneByName(timezone_name.c_str())) {
                return ReturnValue("{\"error\":\"timezone_invalid\",\"message\":\"Unknown timezone_name.\"}");
            }
            clock_sync.syncNow(nullptr, nullptr, 2000);

            auto* ui = mcp->getUi();
            if (!ui) {
                return ReturnValue("{\"error\":\"ui_unavailable\",\"message\":\"UI not available.\"}");
            }

            if (!ui->showClock(timezone_name)) {
                return ReturnValue("{\"error\":\"clock_failed\",\"message\":\"Unable to show clock.\"}");
            }

            String result = "{\"status\":\"ok\",\"timezone_name\":\"" + timezone_name + "\"}";
            return ReturnValue(result);
        }
    );

    mcp->addTool(
        "self.display.stop_clock",
        "Clear the clock display and resume GIF animations.\n"
        "Use this tool for:\n"
        "1. Turning off clock mode when the user asks to hide or stop the clock.\n"
        "Notes: call self.get_device_status first to confirm whether clock mode is active.",
        PropertyList(std::vector<Property>{}),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* ui = mcp->getUi();
            if (!ui) {
                return ReturnValue("{\"error\":\"ui_unavailable\",\"message\":\"UI not available.\"}");
            }

            ui->clearClock();
            return ReturnValue(true);
        }
    );

}

void McpToolRegistry::registerEffectsTools(McpServer* mcp)
{
    // ========================================================================
    // DISPLAY EFFECTS TOOLS
    // ========================================================================

    mcp->addTool(
        "self.display_effects.enable_scanlines",
        "Enable scanline effects on the display (retro CRT look).\n"
        "Use this tool for:\n"
        "1. Turning scanlines on when the user asks for scanline effect.\n"
        "2. Applying scanlines as part of a retro visual request.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setScanlinesEnabled(true);
            if (storage) {
                storage->setEffectsScanlinesEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Scanlines enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.disable_scanlines",
        "Disable scanline effects on the display.\n"
        "Use this tool for:\n"
        "1. Turning scanlines off when the user asks to disable them.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setScanlinesEnabled(false);
            if (storage) {
                storage->setEffectsScanlinesEnabled(false);
            }
            ESP_LOGI(TAG, "[MCP] Scanlines disabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.enable_glitch",
        "Enable glitch effects on the display.\n"
        "Use this tool for:\n"
        "1. Turning on glitch effect when the user asks for glitch visuals.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setGlitchEnabled(true);
            if (storage) {
                storage->setEffectsGlitchEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Glitch enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.disable_glitch",
        "Disable glitch effects on the display.\n"
        "Use this tool for:\n"
        "1. Turning off glitch effect when the user asks to disable it.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setGlitchEnabled(false);
            if (storage) {
                storage->setEffectsGlitchEnabled(false);
            }
            ESP_LOGI(TAG, "[MCP] Glitch disabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.enable_tint_green",
        "Enable a green tint effect on the display (overrides any existing tint).\n"
        "Use this tool for:\n"
        "1. Turning on green tint when the user asks to switch to green color.\n"
        "2. Applying a green/terminal-style tint on request.\n"
        "Notes: call self.get_device_status first to confirm current tint.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            RetroEffects::TintParams tint_params = effects->getTintParams();
            tint_params.tint_color = RetroTints::TINT_GREEN_400;
            effects->setTintParams(tint_params);
            effects->setTintEnabled(true);
            if (storage) {
                storage->setEffectsTintColor(tint_params.tint_color);
                storage->setEffectsTintEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Green tint enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.enable_tint_blue",
        "Enable a blue tint effect on the display (overrides any existing tint).\n"
        "Use this tool for:\n"
        "1. Turning on blue tint when the user asks to switch to blue color.\n"
        "2. Applying a blue screen tint on request.\n"
        "Notes: call self.get_device_status first to confirm current tint.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            RetroEffects::TintParams tint_params = effects->getTintParams();
            tint_params.tint_color = RetroTints::TINT_BLUE_400;
            effects->setTintParams(tint_params);
            effects->setTintEnabled(true);
            if (storage) {
                storage->setEffectsTintColor(tint_params.tint_color);
                storage->setEffectsTintEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Blue tint enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.enable_tint_yellow",
        "Enable a yellow tint effect on the display (overrides any existing tint).\n"
        "Use this tool for:\n"
        "1. Turning on yellow tint when the user asks to switch to yellow color.\n"
        "2. Applying a warm/amber tint on request.\n"
        "Notes: call self.get_device_status first to confirm current tint.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            RetroEffects::TintParams tint_params = effects->getTintParams();
            tint_params.tint_color = RetroTints::TINT_YELLOW_400;
            effects->setTintParams(tint_params);
            effects->setTintEnabled(true);
            if (storage) {
                storage->setEffectsTintColor(tint_params.tint_color);
                storage->setEffectsTintEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Yellow tint enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.disable_tint",
        "Disable the tint effect on the display.\n"
        "Use this tool for:\n"
        "1. Removing any active tint when the user asks to clear tints.\n"
        "2. Switching back to normal/white colors when the user asks to reset colors.\n"
        "Notes: call self.get_device_status first to confirm current tint.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setTintEnabled(false);
            if (storage) {
                storage->setEffectsTintEnabled(false);
            }
            ESP_LOGI(TAG, "[MCP] Tint disabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.enable_dot_matrix",
        "Enable dot matrix effect on the display.\n"
        "Use this tool for:\n"
        "1. Turning on dot matrix effect when the user asks for it.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setDotMatrixEnabled(true);
            if (storage) {
                storage->setEffectsDotMatrixEnabled(true);
            }
            ESP_LOGI(TAG, "[MCP] Dot matrix enabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.disable_dot_matrix",
        "Disable dot matrix effect on the display.\n"
        "Use this tool for:\n"
        "1. Turning off dot matrix effect when the user asks to disable it.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->setDotMatrixEnabled(false);
            if (storage) {
                storage->setEffectsDotMatrixEnabled(false);
            }
            ESP_LOGI(TAG, "[MCP] Dot matrix disabled");
            return ReturnValue(true);
        }
    );

    mcp->addTool(
        "self.display_effects.disable_all",
        "Disable all display effects (scanlines, glitch, dot matrix, and tints).\n"
        "Use this tool for:\n"
        "1. Clearing all active effects when the user asks to reset visuals.\n"
        "2. Removing all effects when the user asks to clear or remove effects.\n"
        "Notes: call self.get_device_status first to confirm current effects state.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            if (!effects) {
                ESP_LOGE(TAG, "[MCP] Effects manager not available");
                return ReturnValue(false);
            }
            effects->disableAll();
            if (storage) {
                storage->setEffectsScanlinesEnabled(false);
                storage->setEffectsGlitchEnabled(false);
                storage->setEffectsDotMatrixEnabled(false);
                storage->setEffectsTintEnabled(false);
            }
            ESP_LOGI(TAG, "[MCP] All effects disabled");
            return ReturnValue(true);
        }
    );
}

void McpToolRegistry::registerStatusTools(McpServer* mcp)
{
    // ========================================================================
    // STATUS TOOLS
    // ========================================================================
    // NOTE: GOOLE API integration example, requires a GOOGLE API KEY:
    // mcp->addTool(
    //     "self.get_weather",
    //     "Fetch current weather for a location using Google Geocoding and Weather APIs.\n"
    //     "Use this tool for:\n"
    //     "1. Answering current weather questions.\n"
    //     "2. Fetching temperature, conditions, humidity, and wind details.\n"
    //     "Notes: provide location or set it with self.set_location. Units are c/f.",
    //     PropertyList({
    //         Property("location", PROPERTY_TYPE_STRING, ""),
    //         Property("unit", PROPERTY_TYPE_STRING, "c")
    //     }),
    //     [mcp](PropertyList& params) -> ReturnValue {
    //         String location = params["location"].getStringValue();
    //         String unit = params["unit"].getStringValue();
    //         unit.toLowerCase();
    //         location.trim();
    //         if (location.isEmpty() && mcp && mcp->getStorage()) {
    //             location = mcp->getStorage()->getLocation();
    //             location.trim();
    //         }

    //         if (GOOGLE_WEATHER_API_KEY[0] == '\0') {
    //             return ReturnValue("{\"error\":\"missing GOOGLE_WEATHER_API_KEY\"}");
    //         }

    //         if (unit != "c" && unit != "f") {
    //             return ReturnValue("{\"error\":\"unit must be 'c' or 'f'\"}");
    //         }

    //         if (location.isEmpty()) {
    //             return ReturnValue("{\"error\":\"location_required\",\"message\":\"Provide location or set it with self.set_location.\"}");
    //         }

    //         String units_system = (unit == "f") ? "IMPERIAL" : "METRIC";
    //         SecureHttpClient client(ROOT_CA_CERTIFICATE);
    //         client.setTimeout(5000);

    //         static String cached_session_id;
    //         static String cached_location;
    //         static String cached_unit;
    //         static String cached_response;

    //         String session_id;
    //         if (mcp) {
    //             session_id = mcp->getSessionId();
    //         }
    //         bool cache_hit = !cached_response.isEmpty() &&
    //                          session_id.length() > 0 &&
    //                          session_id == cached_session_id &&
    //                          location == cached_location &&
    //                          unit == cached_unit;
    //         if (cache_hit) {
    //             return ReturnValue(cached_response);
    //         }

    //         String geocode_url = String("https://maps.googleapis.com/maps/api/geocode/json?address=") +
    //                              urlEncode(location) +
    //                              "&key=" + GOOGLE_WEATHER_API_KEY;
    //         String geocode_body;
    //         if (!client.get(geocode_url, geocode_body)) {
    //             String err = "{\"error\":\"geocode request failed\",\"code\":" +
    //                          String(client.getResponseCode()) + "}";
    //             return ReturnValue(err);
    //         }

    //         JsonDocument geocode_doc;
    //         DeserializationError geocode_error = deserializeJson(geocode_doc, geocode_body);
    //         if (geocode_error) {
    //             return ReturnValue("{\"error\":\"failed to parse geocode response\"}");
    //         }

    //         const char* status = geocode_doc["status"] | "";
    //         if (strcmp(status, "OK") != 0) {
    //             String err = String("{\"error\":\"geocode failed\",\"status\":\"") + status + "\"}";
    //             return ReturnValue(err);
    //         }

    //         JsonObject first_result = geocode_doc["results"][0];
    //         if (first_result.isNull()) {
    //             return ReturnValue("{\"error\":\"geocode returned no results\"}");
    //         }

    //         JsonObject location_obj = first_result["geometry"]["location"];
    //         if (location_obj.isNull()) {
    //             return ReturnValue("{\"error\":\"geocode missing location\"}");
    //         }

    //         double latitude = location_obj["lat"] | 0.0;
    //         double longitude = location_obj["lng"] | 0.0;
    //         const char* formatted_address = first_result["formatted_address"] | "";

    //         String weather_url = String("https://weather.googleapis.com/v1/currentConditions:lookup?key=") +
    //                              GOOGLE_WEATHER_API_KEY +
    //                              "&location.latitude=" + String(latitude, 6) +
    //                              "&location.longitude=" + String(longitude, 6) +
    //                              "&unitsSystem=" + units_system;

    //         String weather_body;
    //         if (!client.get(weather_url, weather_body)) {
    //             String err = "{\"error\":\"weather api request failed\",\"code\":" +
    //                          String(client.getResponseCode()) + "}";
    //             return ReturnValue(err);
    //         }

    //         JsonDocument weather_doc;
    //         DeserializationError weather_error = deserializeJson(weather_doc, weather_body);
    //         if (weather_error) {
    //             return ReturnValue("{\"error\":\"failed to parse weather response\"}");
    //         }

    //         JsonDocument* response = new JsonDocument();
    //         JsonObject weather = (*response)["weather"].to<JsonObject>();
    //         weather["location"] = location;
    //         weather["resolved_address"] = formatted_address;
    //         weather["latitude"] = latitude;
    //         weather["longitude"] = longitude;
    //         weather["unit"] = unit;

    //         JsonObject condition = weather_doc["weatherCondition"]["description"];
    //         if (!condition.isNull()) {
    //             weather["condition"] = condition["text"] | "";
    //         }

    //         JsonObject temp = weather_doc["temperature"];
    //         if (!temp.isNull()) {
    //             weather["temperature_degrees"] = temp["degrees"] | 0.0;
    //             weather["temperature_unit"] = temp["unit"] | "";
    //         }

    //         JsonObject feels_like = weather_doc["feelsLikeTemperature"];
    //         if (!feels_like.isNull()) {
    //             weather["feels_like_degrees"] = feels_like["degrees"] | 0.0;
    //             weather["feels_like_unit"] = feels_like["unit"] | "";
    //         }

    //         weather["relative_humidity"] = weather_doc["relativeHumidity"] | 0;

    //         JsonObject wind = weather_doc["wind"];
    //         if (!wind.isNull()) {
    //             JsonObject speed = wind["speed"];
    //             if (!speed.isNull()) {
    //                 weather["wind_speed"] = speed["value"] | 0.0;
    //                 weather["wind_unit"] = speed["unit"] | "";
    //             }
    //             JsonObject direction = wind["direction"];
    //             if (!direction.isNull()) {
    //                 weather["wind_direction"] = direction["cardinal"] | "";
    //             }
    //         }

    //         String serialized;
    //         serializeJson(*response, serialized);

    //         if (session_id.length() > 0) {
    //             cached_session_id = session_id;
    //             cached_location = location;
    //             cached_unit = unit;
    //             cached_response = serialized;
    //         } else {
    //             cached_session_id = "";
    //             cached_location = "";
    //             cached_unit = "";
    //             cached_response = "";
    //         }

    //         return ReturnValue(response);
    //     }
    // );

    mcp->addTool(
        "self.describe_self",
        "Provide a self-description payload for the assistant.\n"
        "Use this tool for:\n"
        "1. Answering questions like 'who are you' or 'describe yourself'.",
        PropertyList(),
        [](PropertyList& params) -> ReturnValue {
            (void)params;
            JsonDocument* response = new JsonDocument();
            (*response)["name"] = "self.describe_self";
            (*response)["description"] = DESCRIBE_SELF_DESCRIPTION;
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.tell_joke",
        "Guide the assistant to tell 3 jokes themed around retro computing, development, Byte 90, retro gaming, and microcontrollers.\n"
        "Use this tool for:\n"
        "1. When the user asks for jokes or a funny response.",
        PropertyList(),
        [](PropertyList& params) -> ReturnValue {
            (void)params;
            JsonDocument* response = new JsonDocument();
            (*response)["style"] = "Tell 3 short, friendly jokes back-to-back. Pause briefly between each joke and make snarky comments for comedic timing.";
            (*response)["themes"] = "retro computing, development, Byte 90, retro gaming, microcontrollers";
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.get_device_status",
        "Provide real-time device status including audio, display, network, and timer info.\n"
        "Use this tool for:\n"
        "1. Answering questions about current device state (volume, brightness, network).\n"
        "2. As a first step before changing device settings.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            // Access components through McpServer
            auto* audio_codec = mcp->getAudioCodec();
            auto* audio_service = mcp->getAudioService();
            auto* ws_client = mcp->getWsClient();
            auto* display = mcp->getDisplay();
            auto* effects = mcp->getEffectsManager();
            auto* storage = mcp->getStorage();
            auto* timer_manager = mcp->getTimerManager();
            const String& session_id = mcp->getSessionId();

            JsonDocument* status = new JsonDocument();

            // Audio status
            JsonObject audio = (*status)["audio"].to<JsonObject>();
            if (audio_codec != nullptr) {
                audio["volume"] = audio_codec->getOutputVolume();
            } else {
                audio["volume"] = 0;
            }

            // Display status
            JsonObject disp = (*status)["display"].to<JsonObject>();
            if (display != nullptr) {
                disp["brightness"] = display->getBrightnessPercent();
            } else {
                disp["brightness"] = 0;
            }
            if (mcp && mcp->getUi()) {
                disp["clock_mode"] = mcp->getUi()->isShowingClock();
            } else {
                disp["clock_mode"] = false;
            }

            // Effects status
            JsonObject effects_status = (*status)["effects"].to<JsonObject>();
            if (effects != nullptr) {
                bool scanlines = effects->isScanlinesEnabled();
                bool dot_matrix = effects->isDotMatrixEnabled();
                bool glitch = effects->isGlitchEnabled();
                bool tint_enabled = effects->isTintEnabled();

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
                if (tint_enabled) {
                    RetroEffects::TintParams tint_params = effects->getTintParams();
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

                effects_status["effect"] = effect;
                effects_status["tint"] = tint;
                effects_status["scanlines"] = scanlines;
                effects_status["dot_matrix"] = dot_matrix;
                effects_status["glitch"] = glitch;
                effects_status["tint_enabled"] = tint_enabled;
            } else {
                effects_status["effect"] = "unknown";
                effects_status["tint"] = "unknown";
            }

            // Location and timezone
            JsonObject locale = (*status)["locale"].to<JsonObject>();
            if (storage) {
                locale["timezone_name"] = storage->getTimezoneName();
                locale["location"] = storage->getLocation();
            } else {
                locale["timezone_name"] = "";
                locale["location"] = "";
            }

            // Network status
            JsonObject network = (*status)["network"].to<JsonObject>();
            network["connected"] = WiFi.isConnected();
            if (WiFi.isConnected()) {
                network["ssid"] = WiFi.SSID();
                network["rssi"] = WiFi.RSSI();
                network["ip"] = WiFi.localIP().toString();
            }

            // WebSocket status - with null check to prevent crash
            JsonObject ws = (*status)["websocket"].to<JsonObject>();
            ws["connected"] = (ws_client != nullptr && ws_client->isConnected());
            ws["session_id"] = session_id;
            ws["listening"] = (audio_service != nullptr && audio_service->isCaptureActive());

            // Timer status
            JsonObject timer = (*status)["timer"].to<JsonObject>();
            if (timer_manager) {
                timer["running"] = timer_manager->isRunning();
                timer["duration_seconds"] = timer_manager->durationSeconds();
                timer["remaining_seconds"] = timer_manager->remainingSeconds();
                timer["last_duration_seconds"] = timer_manager->lastDurationSeconds();
            } else {
                timer["running"] = false;
                timer["duration_seconds"] = 0;
                timer["remaining_seconds"] = 0;
                timer["last_duration_seconds"] = 0;
            }

            return ReturnValue(status);
        }
    );

    mcp->addTool(
        "self.get_time",
        "Get current device time (syncs via NTP) for a timezone.\n"
        "Use this tool for:\n"
        "1. Answering time questions for a specific timezone.\n"
        "2. Ensuring time is synced before time-based features.\n"
        "Notes: timezone_name is required unless set with self.set_timezone.",
        PropertyList({
            Property(
                "timezone_name",
                PROPERTY_TYPE_STRING
            ).withDescription(
                "User timezone. Use stored timezone from NVS to get the local time."
            )
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            String ntp_server = "";
            String timezone_name = params["timezone_name"].getStringValue();
            timezone_name.trim();

            ClockSync clock_sync;
            if (timezone_name.length() == 0) {
                if (mcp && mcp->getStorage()) {
                    timezone_name = mcp->getStorage()->getTimezoneName();
                    timezone_name.trim();
                }
            }

            if (timezone_name.length() == 0) {
                return ReturnValue("{\"error\":\"timezone_required\",\"message\":\"Please provide timezone_name (e.g., North_America_Eastern, UTC, China) or set it with self.set_timezone.\"}");
            }

            if (!clock_sync.setTimezoneByName(timezone_name.c_str())) {
                return ReturnValue("{\"error\":\"timezone_invalid\",\"message\":\"Unknown timezone_name. Use values like North_America_Eastern, UTC, China.\"}");
            }

            String resolved_tz = clock_sync.getTimezone();
            if (resolved_tz.length() == 0) {
                return ReturnValue("{\"error\":\"timezone_required\",\"message\":\"Please provide timezone_name (e.g., North_America_Eastern, UTC, China) or set it with self.set_timezone.\"}");
            }
            const char* tz_ptr = resolved_tz.length() > 0 ? resolved_tz.c_str() : nullptr;
            const char* server = clock_sync.resolveNtpServer(tz_ptr, ntp_server.c_str());
            bool synced = clock_sync.syncNow(server, tz_ptr);
            bool time_valid = clock_sync.isTimeValid();

            JsonDocument* response = new JsonDocument();
            (*response)["epoch_ms"] = clock_sync.epochMs();
            (*response)["local_iso8601"] = clock_sync.formatLocalIso8601();
            (*response)["utc_iso8601"] = clock_sync.formatUtcIso8601();
            (*response)["time_valid"] = time_valid;
            (*response)["synced"] = synced;
            (*response)["ntp_server"] = server;
            if (resolved_tz.length() > 0) {
                (*response)["timezone"] = resolved_tz;
            }
            if (timezone_name.length() > 0) {
                (*response)["timezone_name"] = timezone_name;
            }
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.set_timezone",
        "Set the default timezone_name for MCP tools and persist it to storage.\n"
        "Use this tool for:\n"
        "1. Saving a user's preferred timezone for future time requests.",
        PropertyList({
            Property("timezone_name", PROPERTY_TYPE_STRING)
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            String timezone_name = params["timezone_name"].getStringValue();
            timezone_name.trim();
            if (timezone_name.isEmpty()) {
                return ReturnValue("{\"error\":\"timezone_required\",\"message\":\"Provide timezone_name (e.g., North_America_Eastern, UTC, China).\"}");
            }

            ClockSync clock_sync;
            if (!clock_sync.setTimezoneByName(timezone_name.c_str())) {
                return ReturnValue("{\"error\":\"timezone_invalid\",\"message\":\"Unknown timezone_name.\"}");
            }

            auto* storage = mcp ? mcp->getStorage() : nullptr;
            if (storage) {
                storage->setTimezoneName(timezone_name.c_str());
            }

            String result = "{\"status\":\"ok\",\"timezone_name\":\"" + timezone_name + "\"}";
            return ReturnValue(result);
        }
    );

    mcp->addTool(
        "self.set_location",
        "Set the default location for MCP tools and persist it to storage.\n"
        "Use this tool for:\n"
        "1. Saving a user's default location for weather queries.",
        PropertyList({
            Property("location", PROPERTY_TYPE_STRING)
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            String location = params["location"].getStringValue();
            location.trim();
            if (location.isEmpty()) {
                return ReturnValue("{\"error\":\"location_required\",\"message\":\"Provide a location (e.g., Toronto, CA).\"}");
            }

            auto* storage = mcp ? mcp->getStorage() : nullptr;
            if (!storage) {
                return ReturnValue("{\"error\":\"storage_unavailable\",\"message\":\"Storage not available.\"}");
            }

            if (!storage->setLocation(location.c_str())) {
                return ReturnValue("{\"error\":\"location_save_failed\",\"message\":\"Failed to save location.\"}");
            }

            String result = "{\"status\":\"ok\",\"location\":\"" + location + "\"}";
            return ReturnValue(result);
        }
    );
}

void McpToolRegistry::registerTimerTools(McpServer* mcp)
{
    // ========================================================================
    // TIMER TOOLS
    // ========================================================================

    mcp->addTool(
        "self.timer.set",
        "Start a single countdown timer using hours OR minutes OR seconds.\n"
        "Use this tool for:\n"
        "1. Creating a new timer when the user asks (e.g., 'set a 10 minute timer').\n"
        "Notes: provide exactly one of hours, minutes, or seconds.",
        PropertyList({
            Property("hours", PROPERTY_TYPE_INTEGER, 0, 0, 8),
            Property("minutes", PROPERTY_TYPE_INTEGER, 0, 0, 480),
            Property("seconds", PROPERTY_TYPE_INTEGER, 0, 0, 28800)
        }),
        [mcp](PropertyList& params) -> ReturnValue {
            auto* timer_manager = mcp ? mcp->getTimerManager() : nullptr;
            if (!timer_manager) {
                return ReturnValue("{\"error\":\"timer_unavailable\",\"message\":\"Timer unavailable.\"}");
            }

            int hours = params["hours"].getIntValue();
            int minutes = params["minutes"].getIntValue();
            int seconds = params["seconds"].getIntValue();

            int provided = 0;
            if (hours > 0) provided++;
            if (minutes > 0) provided++;
            if (seconds > 0) provided++;

            if (provided != 1) {
                return ReturnValue("{\"error\":\"duration_invalid\",\"message\":\"Provide exactly one of hours, minutes, or seconds.\"}");
            }

            uint32_t duration_seconds = 0;
            TimerManager::DisplayFormat format = TimerManager::DisplayFormat::Seconds;
            if (hours > 0) {
                duration_seconds = static_cast<uint32_t>(hours) * 3600U;
                format = TimerManager::DisplayFormat::Hours;
            } else if (minutes > 0) {
                duration_seconds = static_cast<uint32_t>(minutes) * 60U;
                format = TimerManager::DisplayFormat::Minutes;
            } else if (seconds > 0) {
                duration_seconds = static_cast<uint32_t>(seconds);
                format = TimerManager::DisplayFormat::Seconds;
            }

            if (duration_seconds == 0 || duration_seconds > 28800U) {
                return ReturnValue("{\"error\":\"duration_invalid\",\"message\":\"Duration must be between 1 second and 8 hours.\"}");
            }

            if (timer_manager->isRunning()) {
                return ReturnValue("{\"error\":\"timer_running\",\"message\":\"A timer is already running.\"}");
            }

            if (!timer_manager->start(duration_seconds, format)) {
                return ReturnValue("{\"error\":\"timer_start_failed\",\"message\":\"Failed to start timer.\"}");
            }

            ClockSync clock_sync;
            long long now_ms = clock_sync.epochMs();
            long long ends_at = now_ms > 0
                ? now_ms + static_cast<long long>(duration_seconds) * 1000LL
                : 0;

            JsonDocument* response = new JsonDocument();
            (*response)["status"] = "ok";
            (*response)["duration_seconds"] = duration_seconds;
            (*response)["ends_at_epoch_ms"] = ends_at;
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.timer.status",
        "Get the current timer status.\n"
        "Use this tool for:\n"
        "1. Answering questions about whether a timer is running and time remaining.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* timer_manager = mcp ? mcp->getTimerManager() : nullptr;
            if (!timer_manager) {
                return ReturnValue("{\"error\":\"timer_unavailable\",\"message\":\"Timer unavailable.\"}");
            }

            bool running = timer_manager->isRunning();
            uint32_t duration_seconds = running ? timer_manager->durationSeconds() : 0;
            uint32_t remaining_seconds = running ? timer_manager->remainingSeconds() : 0;

            ClockSync clock_sync;
            long long now_ms = clock_sync.epochMs();
            long long ends_at = (running && now_ms > 0)
                ? now_ms + static_cast<long long>(remaining_seconds) * 1000LL
                : 0;

            JsonDocument* response = new JsonDocument();
            (*response)["running"] = running;
            (*response)["duration_seconds"] = duration_seconds;
            (*response)["remaining_seconds"] = remaining_seconds;
            (*response)["ends_at_epoch_ms"] = ends_at;
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.timer.cancel",
        "Cancel the active timer.\n"
        "Use this tool for:\n"
        "1. Stopping a running timer when the user asks to cancel it.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* timer_manager = mcp ? mcp->getTimerManager() : nullptr;
            if (!timer_manager) {
                return ReturnValue("{\"error\":\"timer_unavailable\",\"message\":\"Timer unavailable.\"}");
            }

            if (!timer_manager->isRunning()) {
                return ReturnValue("{\"error\":\"timer_not_running\",\"message\":\"No active timer to cancel.\"}");
            }

            bool canceled = timer_manager->cancel();
            if (!canceled) {
                return ReturnValue("{\"error\":\"timer_cancel_failed\",\"message\":\"Failed to cancel timer.\"}");
            }

            JsonDocument* response = new JsonDocument();
            (*response)["status"] = "ok";
            (*response)["canceled"] = true;
            return ReturnValue(response);
        }
    );

    mcp->addTool(
        "self.timer.repeat",
        "Repeat the last timer duration.\n"
        "Use this tool for:\n"
        "1. Repeating the most recent timer when the user asks to do it again.",
        PropertyList(),
        [mcp](PropertyList& params) -> ReturnValue {
            (void)params;
            auto* timer_manager = mcp ? mcp->getTimerManager() : nullptr;
            if (!timer_manager) {
                return ReturnValue("{\"error\":\"timer_unavailable\",\"message\":\"Timer unavailable.\"}");
            }

            if (timer_manager->isRunning()) {
                return ReturnValue("{\"error\":\"timer_running\",\"message\":\"A timer is already running.\"}");
            }

            uint32_t last_duration = timer_manager->lastDurationSeconds();
            if (last_duration == 0) {
                return ReturnValue("{\"error\":\"no_previous_timer\",\"message\":\"No previous timer to repeat.\"}");
            }

            if (!timer_manager->repeat()) {
                return ReturnValue("{\"error\":\"timer_start_failed\",\"message\":\"Failed to start timer.\"}");
            }

            ClockSync clock_sync;
            long long now_ms = clock_sync.epochMs();
            long long ends_at = now_ms > 0
                ? now_ms + static_cast<long long>(last_duration) * 1000LL
                : 0;

            JsonDocument* response = new JsonDocument();
            (*response)["status"] = "ok";
            (*response)["duration_seconds"] = last_duration;
            (*response)["ends_at_epoch_ms"] = ends_at;
            return ReturnValue(response);
        }
    );
}
