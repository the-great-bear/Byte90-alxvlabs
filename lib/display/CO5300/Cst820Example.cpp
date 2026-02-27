/**
 * Cst820Example.cpp
 *
 * Implementation for Cst820Example.
 */

#if defined(PIO_UNIT_TESTING) || defined(TEST_ENV)
// Skip example sketch during unit tests.
#else

#include "ArduinoCo5300.h"
#include "Cst820Touch.h"
#include <Arduino.h>
#include <esp_log.h>

static const char *TAG = "CST820_Example";

// Pin configuration for Seeed Studio XIAO ESP32S3
#define TFT_CS 44
#define TFT_SCK 7
#define TFT_SDIO0 9
#define TFT_SDIO1 8
#define TFT_SDIO2 43
#define TFT_SDIO3 4
#define TFT_RST -1 // -1 = use board reset

// Display configuration
#define TFT_WIDTH 460
#define TFT_HEIGHT 460
#define TFT_ROTATION 0

// Touch pin configuration
#define TOUCH_SDA 5
#define TOUCH_SCL 6
#define TOUCH_RST -1
#define TOUCH_IRQ -1

// Initialize display
Arduino_CO5300 *gfx = Arduino_CO5300::createDisplay(TFT_CS, TFT_SCK, TFT_SDIO0, TFT_SDIO1, TFT_SDIO2, TFT_SDIO3,
                                                    TFT_RST, TFT_WIDTH, TFT_HEIGHT, TFT_ROTATION);

// Initialize touch
CST820_Touch touch;

// Color palette
uint16_t colors[] = {
    0xF800, // Red
    0xF800, // Orange (approximation)
    0xFFE0, // Yellow
    0x07E0, // Green
    0x001F, // Blue
    0x780F, // Purple
    0xF81F, // Magenta
    0xFFFF  // White
};
const int num_colors = sizeof(colors) / sizeof(colors[0]);
int current_color = 0;

volatile bool touch_detected = false;

void IRAM_ATTR touchIsr() {
    touch_detected = true;
}

void setup(void) {
    ESP_LOGI(TAG, ":::: CO5300 Touch Demo ::::");

    // Initialize display
    if (!gfx->begin()) {
        ESP_LOGE(TAG, "❌  Display init failed!");
        while (1)
            delay(1000);
    }
    ESP_LOGI(TAG, "✅  Display ready!");

    // Initialize touch
    ESP_LOGI(TAG, "Initializing touch...");
    if (!touch.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_IRQ)) {
        ESP_LOGE(TAG, "❌  Touch init failed!");
        ESP_LOGE(TAG, "Chip ID: 0x%X", touch.getChipID());
    } else {
        ESP_LOGI(TAG, "✅  Touch ready! Chip: %s", touch.getModelName());
        ESP_LOGI(TAG, "Chip ID: 0x%X", touch.getChipID());
        ESP_LOGI(TAG, "FW Version: 0x%X", touch.getFirmwareVersion());

        // Configure touch
        touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);

        // Disable auto-sleep for continuous touch detection
        touch.disableAutoSleep();
        ESP_LOGI(TAG, "Auto-sleep disabled");
    }

    // Setup touch interrupt
    if (TOUCH_IRQ != -1) {
        pinMode(TOUCH_IRQ, INPUT);
        attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), touchIsr, FALLING);
    }

    // Initial screen
    gfx->fillScreen(colors[current_color]);
    gfx->setTextColor(0x0000);
    gfx->setTextSize(3);
    gfx->setCursor(100, 200);
    gfx->println("Touch to change color!");
}

void loop() {
    // Check for touch (polling mode since no IRQ pin)
    static uint32_t last_check = 0;
    if (millis() - last_check > 100) {
        last_check = millis();
        if (touch.isTouched()) {
            ESP_LOGD(TAG, "Touch detected!");
        }
    }

    int16_t x, y;
    if (touch.getTouch(x, y)) {
        // Change color based on touch position
        // Divide screen into horizontal zones
        if (x < TFT_WIDTH / 4) {
            // Left quarter - cycle through colors
            current_color = (current_color + 1) % num_colors;
        } else if (x < TFT_WIDTH / 2) {
            // Second quarter - warm colors (red/orange/yellow)
            current_color = random(0, 3);
        } else if (x < (TFT_WIDTH * 3) / 4) {
            // Third quarter - cool colors (green/blue/purple)
            current_color = random(3, 6);
        } else {
            // Right quarter - random color
            current_color = random(0, num_colors);
        }

        // Update display
        gfx->fillScreen(colors[current_color]);

        // Draw touch indicator
        gfx->fillCircle(x, y, 20, 0xFFFF);
        gfx->drawCircle(x, y, 21, 0x0000);

        // Show coordinates
        gfx->setTextColor(0xFFFF);
        gfx->setTextSize(2);
        gfx->setCursor(10, 10);
        gfx->print("X:");
        gfx->print(x);
        gfx->print(" Y:");
        gfx->print(y);

        // Show zone info
        gfx->setCursor(10, 40);
        if (x < TFT_WIDTH / 4) {
            gfx->print("Zone: LEFT (Cycle)");
        } else if (x < TFT_WIDTH / 2) {
            gfx->print("Zone: MID-L (Warm)");
        } else if (x < (TFT_WIDTH * 3) / 4) {
            gfx->print("Zone: MID-R (Cool)");
        } else {
            gfx->print("Zone: RIGHT (Random)");
        }

        ESP_LOGI(TAG, "Touch at X:%d Y:%d Color:%d", x, y, current_color);

        // Debounce delay
        delay(200);
    }

    delay(10);
}

#endif  // PIO_UNIT_TESTING || TEST_ENV
