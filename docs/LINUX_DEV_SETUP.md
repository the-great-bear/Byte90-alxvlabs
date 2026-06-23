# Linux Dev Environment Setup (ESP-IDF + Arduino)

Tested on Ubuntu 24.04 with an ESP32-S3 (QFN56, 8MB PSRAM) connected via USB.

## Device Detection

The Byte90 appears as `/dev/ttyACM0` (USB JTAG/serial, not a CH340/CP2102).

```bash
lsusb | grep Espressif
# 303a:1001 Espressif USB JTAG/serial debug unit

esptool --port /dev/ttyACM0 chip-id
# Chip: ESP32-S3, 240MHz, 8MB PSRAM, Wi-Fi + BT5
# MAC:  10:20:ba:03:bd:b8
```

Add your user to `dialout` (takes effect after re-login), or chmod for the current session:

```bash
sudo usermod -aG dialout $USER
# or per-session:
sudo chmod a+rw /dev/ttyACM0
```

## ESP-IDF Setup

```bash
# Install system dependencies
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Clone ESP-IDF (ESP32-S3 toolchain only)
git clone --recursive --depth 1 --branch v5.4.1 \
  https://github.com/espressif/esp-idf.git ~/esp-idf

# Install tools for ESP32-S3
cd ~/esp-idf && ./install.sh esp32s3

# Add to shell (already added to ~/.bashrc)
echo '. $HOME/esp-idf/export.sh > /dev/null 2>&1' >> ~/.bashrc

# Verify
source ~/esp-idf/export.sh && idf.py --version
# ESP-IDF v5.4.1
```

### Build and Flash with ESP-IDF

```bash
source ~/esp-idf/export.sh

idf.py create-project my_project && cd my_project
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Arduino IDE Setup

Arduino IDE 2.3.6 AppImage is at `~/arduino-ide.AppImage`.

```bash
~/arduino-ide.AppImage --no-sandbox
```

A desktop launcher is at `~/.local/share/applications/arduino-ide.desktop`.

### ESP32 Board Support (already installed)

```bash
arduino-cli core list
# esp32:esp32   3.3.10   3.3.10   esp32
```

Board to select in IDE: **ESP32S3 Dev Module**  
Port: `/dev/ttyACM0`

### Install libraries via CLI

```bash
arduino-cli lib install "Adafruit ST7735 and ST7789 Library" "Adafruit GFX Library"
```

### Build and Flash with arduino-cli

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 my_sketch/
arduino-cli upload  --fqbn esp32:esp32:esp32s3 --port /dev/ttyACM0 my_sketch/
```

## esptool

Installed system-wide via pip:

```bash
esptool --port /dev/ttyACM0 chip-id
esptool --port /dev/ttyACM0 flash_id
esptool --port /dev/ttyACM0 read_flash 0 ALL backup.bin
esptool --port /dev/ttyACM0 write_flash 0 firmware.bin
```

## Display Wiring (1.8" SPI TFT — ST7735)

| Signal | GPIO |
|--------|------|
| MOSI   | 11   |
| SCLK   | 12   |
| CS     | 10   |
| DC     | 9    |
| RST    | 8    |
| BL     | 3.3V |

128×160 resolution. Use `INITR_BLACKTAB` with the Adafruit ST7735 library.
