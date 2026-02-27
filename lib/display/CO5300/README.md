# Arduino_GFX_local - CO5300 AMOLED Display & CST820 Touch Driver

Consolidated display and touch drivers for CO5300 AMOLED displays with CST820 touch controllers.

## Overview

This library provides standalone drivers for:
- **CO5300 AMOLED Display** (460x460 pixels)
- **ESP32 QSPI Interface** (Quad SPI for high-speed data transfer)
- **CST820 Touch Controller** (Capacitive touch with gesture support)

All drivers are optimized, dependency-free (except Adafruit_GFX), and designed for ESP32-S3.

---

## Hardware Support

### Display Controller: CO5300
- **Resolution:** 460x460 pixels (configurable up to 480x480)
- **Interface:** QSPI (Quad SPI)
- **Color Format:** RGB565 (16-bit)
- **Features:** Hardware rotation, brightness control, contrast adjustment, sleep mode

### Touch Controller: CST820/CST816 Family
- **Supported Chips:** CST816S, CST816T, CST816D, CST820, CST716
- **Interface:** I2C (address 0x15)
- **Touch Points:** Single touch
- **Features:** Auto-sleep, home button detection, coordinate transformation

---

## Library Structure

```
lib/Arduino_GFX_local/
├── Arduino_CO5300.cpp/h       # Display controller driver
├── Arduino_ESP32QSPI.cpp/h    # QSPI hardware interface
└── CST820_Touch.cpp/h         # Touch controller driver
```

---

## Display Driver (Arduino_CO5300)

### Architecture

The display driver is built in two layers:

1. **Arduino_ESP32QSPI** - Low-level QSPI hardware interface
   - Manages ESP32 QSPI peripheral
   - Handles DMA transfers for fast pixel data
   - Provides batch operations for initialization sequences

2. **Arduino_CO5300** - High-level display controller
   - Extends Adafruit_GFX for graphics primitives
   - Implements TFT optimizations (address window caching)
   - Provides display-specific functions (brightness, sleep, etc.)

### Key Features

#### Address Window Caching
The driver caches the current drawing window to avoid redundant SPI commands, providing **50-100x speedup** for operations like `fillScreen()`.

#### QSPI Performance
Uses Quad SPI (4 data lines) for **4x faster** data transfer compared to standard SPI:
- Standard SPI: 1 bit per clock cycle
- QSPI: 4 bits per clock cycle

#### Factory Method
Simplified initialization with sensible defaults:
```cpp
Arduino_CO5300::createDisplay(cs, sck, sdio0, sdio1, sdio2, sdio3, rst, width, height, rotation)
```

### Display Functions

#### Initialization
```cpp
Arduino_CO5300 *gfx = Arduino_CO5300::createDisplay(
    44, 7, 9, 8, 43, 4,  // QSPI pins: CS, SCK, SDIO0-3
    -1,                   // RST pin (-1 = use board reset)
    460, 460,            // Width, Height
    0                    // Rotation (0-3)
);

if (!gfx->begin()) {
    // Initialization failed
}
```

#### Drawing (Adafruit_GFX)
All standard Adafruit_GFX functions are available:
```cpp
gfx->fillScreen(color);
gfx->drawPixel(x, y, color);
gfx->drawLine(x0, y0, x1, y1, color);
gfx->drawRect(x, y, w, h, color);
gfx->fillRect(x, y, w, h, color);
gfx->drawCircle(x, y, r, color);
gfx->fillCircle(x, y, r, color);
gfx->drawTriangle(x0, y0, x1, y1, x2, y2, color);
gfx->fillTriangle(x0, y0, x1, y1, x2, y2, color);
gfx->setCursor(x, y);
gfx->setTextColor(color);
gfx->setTextSize(size);
gfx->print("text");
```

#### Display Control
```cpp
// Rotation (0-3)
gfx->setRotation(rotation);

// Brightness (0-255)
gfx->setBrightness(brightness);

// Contrast (0=off, 1=low, 2=medium, 3=high)
gfx->setContrast(CO5300_CONTRAST_HIGH);

// Display inversion
gfx->invertDisplay(true);

// Power management
gfx->displayOn();
gfx->displayOff();
```

#### Optimized Drawing
For best performance, use the write functions inside `startWrite()`/`endWrite()`:
```cpp
gfx->startWrite();
gfx->writePixel(x, y, color);
gfx->writeFillRect(x, y, w, h, color);
gfx->endWrite();
```

### Configuration Defines

Located in `Arduino_CO5300.h`:
```cpp
// Display dimensions
#define CO5300_DEFAULT_WIDTH 460
#define CO5300_DEFAULT_HEIGHT 460
#define CO5300_DEFAULT_ROTATION 0

// Hardware configuration
#define CO5300_DEFAULT_RST -1
#define CO5300_DEFAULT_IPS false

// Display offsets (for panel alignment)
#define CO5300_DEFAULT_COL_OFFSET1 6
#define CO5300_DEFAULT_ROW_OFFSET1 0
#define CO5300_DEFAULT_COL_OFFSET2 0
#define CO5300_DEFAULT_ROW_OFFSET2 0

// Timing delays (ms)
#define CO5300_RST_DELAY 200
#define CO5300_SLPIN_DELAY 120
#define CO5300_SLPOUT_DELAY 120
```

---

## Touch Driver (CST820_Touch)

### Supported Chips

The driver auto-detects the following chips:
- **CST816S** (ID: 0xB4)
- **CST816T** (ID: 0xB5)
- **CST816D** (ID: 0xB6)
- **CST820** (ID: 0xB7)
- **CST716** (ID: 0x20)

### Touch Functions

#### Initialization
```cpp
CST820_Touch touch;

if (!touch.begin(sda, scl, rst, irq)) {
    // Initialization failed
}

// Get chip information
const char* model = touch.getModelName();  // "CST820"
uint8_t chipID = touch.getChipID();        // 0xB7
uint8_t fwVer = touch.getFirmwareVersion(); // Firmware version
```

#### Touch Detection

**Polling Mode** (no IRQ pin):
```cpp
void loop() {
    int16_t x, y;
    if (touch.getTouch(x, y)) {
        // Touch detected at (x, y)
    }
}
```

**Interrupt Mode** (with IRQ pin):
```cpp
volatile bool touched = false;

void IRAM_ATTR touchISR() {
    touched = true;
}

void setup() {
    attachInterrupt(digitalPinToInterrupt(IRQ_PIN), touchISR, FALLING);
}

void loop() {
    if (touched) {
        touched = false;
        int16_t x, y;
        if (touch.getTouch(x, y)) {
            // Handle touch
        }
    }
}
```

#### Coordinate Transformation
```cpp
// Set display dimensions for coordinate mapping
touch.setMaxCoordinates(460, 460);

// Swap X and Y coordinates
touch.setSwapXY(true);

// Mirror coordinates
touch.setMirrorXY(true, false);  // Mirror X, not Y
```

#### Power Management
```cpp
// Sleep mode (reduces power consumption)
touch.sleep();

// Wake from sleep
touch.wakeup();

// Auto-sleep control
touch.disableAutoSleep();  // Stay awake
touch.enableAutoSleep();   // Allow automatic sleep
```

#### Home Button (Virtual Button)
Some displays have a virtual home button at specific coordinates:
```cpp
// Set home button location
touch.setCenterButtonCoordinate(230, 400);

// Set callback for home button press
touch.setHomeButtonCallback([](void *user_data) {
    Serial.println("Home button pressed!");
}, nullptr);
```

### Touch Registers

Key CST820 registers (defined in `CST820_Touch.h`):
```cpp
#define CST820_REG_STATUS       0x00  // Touch status
#define CST820_REG_TOUCH_NUM    0x02  // Number of touch points
#define CST820_REG_XPOS_H       0x03  // X position high byte
#define CST820_REG_XPOS_L       0x04  // X position low byte
#define CST820_REG_YPOS_H       0x05  // Y position high byte
#define CST820_REG_YPOS_L       0x06  // Y position low byte
#define CST820_REG_CHIP_ID      0xA7  // Chip ID
#define CST820_REG_FW_VERSION   0xA9  // Firmware version
#define CST820_REG_SLEEP        0xE5  // Sleep control
#define CST820_REG_DIS_AUTOSLEEP 0xFE // Auto-sleep disable
```

---

## QSPI Driver (Arduino_ESP32QSPI)

### Low-Level Interface

The QSPI driver handles all hardware communication with the display. It's used internally by Arduino_CO5300 but can be accessed directly if needed.

### Key Features

- **DMA Transfers:** Uses ESP32 DMA for efficient data transfer
- **Batch Operations:** Optimized initialization sequences
- **Buffer Management:** Double-buffering for smooth updates
- **Command Modes:** Supports various SPI transaction types

### QSPI Configuration

```cpp
#define ESP32QSPI_MAX_PIXELS_AT_ONCE 1024  // Buffer size
#define ESP32QSPI_FREQUENCY 80000000       // 80 MHz
#define ESP32QSPI_SPI_MODE SPI_MODE0       // SPI mode
#define ESP32QSPI_SPI_HOST SPI2_HOST       // SPI peripheral
#define ESP32QSPI_DMA_CHANNEL SPI_DMA_CH_AUTO  // DMA channel
```

### Pin Requirements

All 6 QSPI pins are required:
- **CS** - Chip Select
- **SCK** - Clock
- **SDIO0-3** - 4 data lines (Quad SPI)

---

## Example Usage

### Complete Example

```cpp
#include <Arduino.h>
#include "Arduino_CO5300.h"
#include "CST820_Touch.h"

// Display pins
#define TFT_CS    44
#define TFT_SCK   7
#define TFT_SDIO0 9
#define TFT_SDIO1 8
#define TFT_SDIO2 43
#define TFT_SDIO3 4
#define TFT_RST   -1

// Touch pins
#define TOUCH_SDA 5
#define TOUCH_SCL 6
#define TOUCH_RST -1
#define TOUCH_IRQ -1

// Initialize display
Arduino_CO5300 *gfx = Arduino_CO5300::createDisplay(
    TFT_CS, TFT_SCK, TFT_SDIO0, TFT_SDIO1, TFT_SDIO2, TFT_SDIO3,
    TFT_RST, 460, 460, 0
);

// Initialize touch
CST820_Touch touch;

void setup() {
    Serial.begin(115200);
    
    // Initialize display
    if (!gfx->begin()) {
        Serial.println("Display init failed!");
        while(1);
    }
    
    // Initialize touch
    if (!touch.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_IRQ)) {
        Serial.println("Touch init failed!");
    } else {
        Serial.println(touch.getModelName());
        touch.setMaxCoordinates(460, 460);
        touch.disableAutoSleep();
    }
    
    // Draw something
    gfx->fillScreen(0x001F);  // Blue
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(3);
    gfx->setCursor(100, 200);
    gfx->println("Hello World!");
}

void loop() {
    int16_t x, y;
    if (touch.getTouch(x, y)) {
        // Draw circle at touch point
        gfx->fillCircle(x, y, 10, 0xFFFF);
        
        Serial.print("Touch: ");
        Serial.print(x);
        Serial.print(", ");
        Serial.println(y);
        
        delay(50);
    }
}
```

---

## Color Format (RGB565)

The display uses 16-bit RGB565 color format:
- **Red:** 5 bits (0-31)
- **Green:** 6 bits (0-63)
- **Blue:** 5 bits (0-31)

### Common Colors
```cpp
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
```

### Color Conversion
```cpp
// RGB888 to RGB565
uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

// Or use Adafruit_GFX helper
uint16_t color = gfx->color565(r, g, b);
```

---

## Performance Tips

1. **Use `startWrite()`/`endWrite()`** for multiple operations
2. **Batch drawing operations** to minimize SPI overhead
3. **Use `fillRect()`** instead of multiple `drawPixel()` calls
4. **Disable auto-sleep** on touch for continuous detection
5. **Use DMA-friendly buffer sizes** (multiples of 1024 pixels)

---

## Troubleshooting

### Display Issues

**Display not initializing:**
- Check QSPI pin connections (all 6 pins required)
- Verify power supply (3.3V, sufficient current)
- Check SPI frequency (try reducing from 80MHz to 40MHz)

**Wrong colors:**
- Check RGB565 color format
- Try `invertDisplay(true)`

**Slow updates:**
- Ensure QSPI is enabled (not standard SPI)
- Use optimized drawing functions
- Check DMA configuration

### Touch Issues

**Touch not detected:**
- Verify I2C address (0x15)
- Check SDA/SCL connections
- Scan I2C bus to confirm device presence
- Try disabling auto-sleep

**Wrong coordinates:**
- Use `setMaxCoordinates()` to match display size
- Try `setSwapXY()` or `setMirrorXY()`
- Check rotation setting

**Intermittent detection:**
- Add debounce delay
- Check IRQ pin connection (if used)
- Disable auto-sleep

---

## Technical Details

### Memory Usage
- **Display buffer:** 2 x 1024 pixels x 2 bytes = 4KB (DMA)
- **Touch buffer:** ~32 bytes
- **Code size:** ~15KB (display) + ~3KB (touch)

### Performance
- **QSPI speed:** 80 MHz (configurable)
- **Full screen update:** ~50ms (460x460 pixels)
- **Touch polling rate:** ~100 Hz
- **Touch IRQ latency:** <1ms

### Power Consumption
- **Display active:** ~50-100mA (depends on brightness)
- **Display sleep:** <1mA
- **Touch active:** ~2mA
- **Touch sleep:** ~1µA (CST816T)

---

## License

MIT License - See individual source files for details.

## Credits

- Display driver consolidated from Arduino_GFX library
- Touch driver based on SensorLib CST816 implementation
- Optimized and simplified for standalone use

---

## Version History

- **v1.0** - Initial consolidated release
  - CO5300 display driver with QSPI
  - CST820 touch driver
  - Adafruit_GFX integration
  - Factory method for easy initialization
