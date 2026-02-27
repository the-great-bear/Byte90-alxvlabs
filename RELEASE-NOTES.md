# 🎉 BYTE 90 v3.0.0 - Series 2 AI Edition Release

We’re excited to introduce BYTE 90 v3.0.0, built specifically for the Series 2 AI Edition.

This release delivers a richer on-device experience, full AI enablement, expanded MCP tools, and a brand-new BYTE 90 configuration captive portal.

---

## 🧩 Hardware Requirements

- **Required:** Series 2 AI Edition hardware
- Upgrade Path: Existing Series 2 owners must purchase the **Series 2 AI Edition Upgrade Kit** to install this firmware
- Series 1 not supported

---

## ✨ What's New

### 🕹️ Captive Portal Configuration UI

The new on-device captive portal now includes a full configuration interface — and it’s the recommended way to manage your BYTE 90.

- Configure Wi-Fi and core system settings
- Customize themes and device preferences
- Access everything directly via the BYTE90-Config portal

### AI Enablement

BYTE 90 now supports AI integration through:
- Xiaozhi AI – Free for personal development projects (Commercial licenses available separately)
- OpenAI – Requires a pay-per-use API key

Install the firmware version that matches your preferred AI service.

### 🧭 Device Intelligence Tools

New MCP tools expand what the AI agent can do on-device:

- **Themes & Effects**: quickly set and switch or reset themes and effects
- **Timers**: set, repeat, cancel, and check active timers
- **Status & controls**: real-time device status plus volume/brightness control

MCP tools also enable custom API integrations — perfect for smart home control and advanced automation.
---

## 📦 Installation Guide

> **⚠️ IMPORTANT**: Version 3.0.0 requires a partition change to accommodate the expanded firmware. This update requires Visual Studio Code and PlatformIO on your computer.

### Prerequisites: Install VS Code & PlatformIO

1. **Download and install VS Code** (if not already installed)  
   [Download VS Code →](https://code.visualstudio.com/)

2. **Install the PlatformIO extension**  
   Install via the Extensions tab in VS Code  
   [Get PlatformIO IDE →](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)

3. **Download the source code**  
   Download the source folder from the link below and open it in VS Code

### Series 2 Users: Firmware Update Steps

1. **Upload firmware**  
   - Click the PlatformIO icon (alien icon) in the sidebar
   - Under **PROJECT TASKS**, locate and click **Upload**
   - Wait for the firmware upload to complete

3. **Connect to Web Configuration Portal**  
   - Connect BYTE 90 to your computer via USB-C
   - Visit the [Web Configuration Portal](https://install.alxv.dev/)
   - Click **Connect** and select your device

4. **Update filesystem**  
   - Download the `filesystem.bin` file
   - Select this file in the portal to update your BYTE 90's partition
   - Wait for the update to complete (see notes below)

5. **Complete!**  
   After the update finishes, your device will restart automatically. Enjoy BYTE 90 v3.0.0!

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

- **Full Changelog**: [View all changes →](https://github.com/alxv2016/Byte90-alxvlabs/commits/v3.0.0)
- **Web Configuration Portal**: [install.alxv.dev](https://install.alxv.dev/)
- **Support**: [labs.alxvtoronto.com/pages/support](https://labs.alxvtoronto.com/pages/support)

*Thank you for being part of the BYTE 90 community! We can't wait to see what you create with v3.0.0.*

---
