/**
 * main.cpp
 *
 * Application entry point.
 */

#include "Application.h"
#include "ApplicationAudio.h"
#include "ArduinoSSD1351.h"
#include "AudioCodec.h"
#include "DeviceConfig.h"

#include "NvsStorage.h"
#include "SerialClient.h"
#include "SystemState.h"
#include "WifiManager.h"
#include "Axp2101.h"
#include "I2CManager.h"
#include "HapticsManager.h"
#include "LittlefsManager.h"
#include "LanguageManager.h"
#include "Adxl345.h"
#include "AdxlManager.h"
#include "TaskManager.h"

#include <Arduino.h>
#include <Adafruit_DRV2605.h>
#include <SPI.h>
#include <esp_log.h>

// Startup image data
#include "StartupImage.h"

static const char* TAG = "main";

// Global application instance
static Application* g_app = nullptr;

void setup() {
    // Delay to allow serial monitor to attach
    Serial.begin(115200);
    // Initialize TaskManager before any task creation
    if (!TaskManager::instance().begin()) {
        ESP_LOGE(TAG, "Failed to initialize TaskManager");
    }
    // ========================================================================
    // Hardware Component Initialization
    // ========================================================================
    // 1. I2C Bus Manager - Initialize shared I2C bus first
    I2CManager& i2c = I2CManager::getInstance();
    if (!i2c.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    }

    // 2. Power Management (AXP2101) - MUST be early to ensure stable power
    AXP2101* power_manager = new AXP2101(&i2c, AXP2101_I2C_ADDR);
    if (!power_manager->begin()) {
        delete power_manager;
        power_manager = nullptr;
    }

    // 3. Haptics Manager (DRV2605L) - Provides haptic feedback for audio events
    HapticsManager* haptics_manager = new HapticsManager(&i2c);
    if (!haptics_manager->begin(HapticsManager::ACTUATOR_ERM)) {
        delete haptics_manager;
        haptics_manager = nullptr;
    } else {
    }

    // 4. ADXL345 Accelerometer
    Adxl345* adxl = new Adxl345();
    bool adxl_ready = adxl->begin(&i2c);
    if (!adxl_ready) {
        delete adxl;
        adxl = nullptr;
    }

    // 5. Display (SSD1351)
    ArduinoSSD1351* display = new ArduinoSSD1351(
        DISPLAY_SPI_CS_PIN,
        DISPLAY_DC_PIN,
        DISPLAY_RESET_PIN,
        DISPLAY_SPI_SCK_PIN,
        DISPLAY_SPI_MOSI_PIN
    );
    if (display->begin()) {
        display->setBrightnessPercent(100);
        display->getAdafruitDisplay()->fillScreen(COLOR_BLACK);
    }

    // 7. NVS Storage (needed for configuration)
    NVSStorage* storage = new NVSStorage();
    storage->begin();

    if (display && storage) {
        system_settings_t system{};
        if (storage->loadSystemSettings(&system)) {
            display->setBrightnessPercent(system.brightness);
        }
    }

    // Load or generate device UUID from NVS (cached in NVSStorage for reuse)
    String device_uuid = storage->getDeviceUUID();
    ESP_LOGI(TAG, "Device UUID: %s", device_uuid.c_str());

    // 8. LittleFS Filesystem
    LittleFSManager* filesystem = new LittleFSManager();
    filesystem->begin();

    // 9. Language Manager
    LanguageManager* language_manager = new LanguageManager(filesystem);
    language_manager->begin();

    // Load saved language preference
    if (storage) {
        String saved_lang = storage->getLanguage();
        if (!saved_lang.isEmpty() && saved_lang.length() > 0) {
            language_manager->setLanguage(saved_lang.c_str());
        }
    }

    // 10. WiFi Manager
    WifiManager* wifi_client = new WifiManager();
    wifi_client->configureCaptivePortal(storage, filesystem);

    // 6. Motion Manager (after display, storage, and WiFi are initialized)
    AdxlManager* adxl_manager = nullptr;
    if (adxl_ready) {
        adxl_manager = new AdxlManager(
            adxl,
            haptics_manager,
            display,
            wifi_client,
            storage,
            power_manager,
            LIGHT_SLEEP_INTERVAL_MS);
    }

    // 11. System State Manager (manages WiFi initialization, callbacks, and state)
    SystemStateManager* state_manager = new SystemStateManager();
    state_manager->begin(wifi_client, language_manager);

    // Initialize WiFi with callbacks and AP logic - SystemStateManager handles everything
    state_manager->initializeWiFi(storage);

    // 12. Serial Client
    SerialClient* serial_client = new SerialClient(wifi_client, storage);
    serial_client->begin();

    // 13. Audio System
    AudioCodec* audio_codec = new AudioCodec(
        AUDIO_SAMPLE_RATE_STT,
        AUDIO_SAMPLE_RATE_TTS,
        AUDIO_SPEAKER_BCLK,
        AUDIO_SPEAKER_LRC,
        AUDIO_SPEAKER_DOUT,
        AUDIO_MIC_I2S_DATA,
        storage
    );

    if (!audio_codec->begin()) {
        // Handle error? For now just log, maybe delete.
    } else {
        audio_codec->start();
    }

    // ========================================================================
    // Application Initialization
    // ========================================================================

    g_app = new Application(
        storage,
        filesystem,
        wifi_client,
        serial_client,
        state_manager,
        power_manager,
        haptics_manager,
        audio_codec,
        language_manager,
        display,
        adxl,
        adxl_manager
    );

    g_app->initialize();

    // Play startup success haptic feedback
    if (haptics_manager && haptics_manager->isReady()) {
        haptics_manager->playEffect(HAPTIC_STRONG_CLICK_100);
        ESP_LOGI(TAG, "Startup complete - haptic feedback played");
    }

}

void loop() {
    if (g_app) {
        g_app->loop();
    }
}
