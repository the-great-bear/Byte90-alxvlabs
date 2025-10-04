# 🎉 BYTE 90 v2.0.0 - Major Feature Release

We're thrilled to announce BYTE 90 v2.0.0, launching alongside Series 2! This milestone release brings powerful new features while maintaining full backward compatibility with Series 1 devices.

---

## ✨ What's New

### 🕹️ Web Configuration Portal

The Web Installer has evolved into a comprehensive configuration and update portal, now the **recommended method** for managing your BYTE 90.

- **Access**: [install.alxv.dev](https://install.alxv.dev/)
- **Requirements**: Desktop or laptop browser with Web Serial API support
- ⚠️ **Important**: Mobile browsers are not supported

### 🎨 Enhanced Menu System

Navigate and customize your BYTE 90 with an intuitive new menu interface designed for effortless control.

- Complete usage instructions available on our [support page](https://labs.alxvtoronto.com/pages/support)

### 🌈 Themes & Visual Effects

Personalize your BYTE 90 experience with customizable themes and effects.

- Settings automatically persist across restarts
- Multiple theme options to choose from
- Real-time effect customization

### ⏰ Series 2 Exclusive Features

Series 2 hardware unlocks enhanced capabilities:

- **Audio & Haptic Feedback**: Immersive physical and auditory responses to interactions
- **Clock Mode**: Transform your BYTE 90 into a desktop timepiece using the built-in RTC module

---

## ⚠️ Known Issues

**Windows 11 WiFi Connectivity**
- Automatic IP assignment may fail when connecting to BYTE 90's access point
- **Workaround**: Manually assign an IP address, or use macOS/iOS/Android (works seamlessly)

**Web Serial API Updates**
- Firmware updates may occasionally timeout
- **Solution**: Simply retry the update process if this occurs

---

## 📦 Installation Guide

> **⚠️ IMPORTANT**: Version 2.0.0 requires a partition change to accommodate the expanded firmware. This update requires Visual Studio Code and PlatformIO on your computer.

### Prerequisites: Install VS Code & PlatformIO

1. **Download and install VS Code** (if not already installed)  
   [Download VS Code →](https://code.visualstudio.com/)

2. **Install the PlatformIO extension**  
   Install via the Extensions tab in VS Code  
   [Get PlatformIO IDE →](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)

3. **Download the source code**  
   Download the source folder from the link below and open it in VS Code

### Series 1 Users: Firmware Update Steps

1. **Configure for Series 1**  
   Navigate to `include/common.h` and set:
   ```cpp
   #define SERIES_2 false
   ```

2. **Upload firmware**  
   - Click the PlatformIO icon (alien icon) in the sidebar
   - Under **PROJECT TASKS**, locate and click **Upload**
   - Wait for the firmware upload to complete

3. **Connect to Web Configuration Portal**  
   - Your device will enter "Crash Mode" after upload - this is expected
   - Connect BYTE 90 to your computer via USB-C
   - Visit the [Web Configuration Portal](https://install.alxv.dev/)
   - Click **Connect** and select your device

4. **Update filesystem**  
   - Download and unzip the `filesystem.bin` file
   - Select this file in the portal to update your BYTE 90's partition
   - Wait for the update to complete (see notes below)

5. **Complete!**  
   After the update finishes, your device will restart automatically. Enjoy BYTE 90 v2.0.0!

> **📝 Update Notes**:
> - The filesystem update can take several minutes due to Web Serial API transfer speeds
> - Keep the browser window active and do not close it during the update to prevent interruptions
> - If the update fails, simply retry the process
> - This partition update is a **one-time requirement** - future updates will be much simpler

---

## 🚀 Getting Started

### 📺 Video Tutorial
Step-by-step installation guide: **Coming Soon**

### 📚 Documentation
- [README](README.md) - Complete setup and user guide
- [Contributing Guidelines](CONTRIBUTING.md) - Join our development community

### 🐛 Found a Bug?
Report issues through our [issue tracker](../../issues)

---

## 🔗 Resources

- **Full Changelog**: [View all changes →](https://github.com/alxv2016/Byte90-alxvlabs/commits/v2.0.0)
- **Web Configuration Portal**: [install.alxv.dev](https://install.alxv.dev/)
- **Support**: [labs.alxvtoronto.com/pages/support](https://labs.alxvtoronto.com/pages/support)

*Thank you for being part of the BYTE 90 community! We can't wait to see what you create with v2.0.0.*

---
