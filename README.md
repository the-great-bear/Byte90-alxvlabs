# BYTE-90 3.0 Firmware

**Open-source firmware for the BYTE-90 AI Interactive Designer Toy**

BYTE-90 is a retro PC and Mac inspired interactive designer art toy that integrates Xiaozhi AI (Tenclass pipeline). It detects motion, responds to taps and orientation changes with animated emotes. Designed and 3D printed by ALXV LABS (Alex Vong) as a designer toy, BYTE-90 is available as either a dev-kit or a collectible designer toy. This product is a limited run, and your support enables continued future development.

This repository contains the **open-source firmware foundation** that powers motion interaction, animation rendering, audio capture and streaming, AI connectivity, and system control.

Product information: [BYTE-90 by ALXV Labs](https://labs.alxvtoronto.com/)

> This project provides firmware only. Proprietary animations, original designs, branding, and 3D printed files are **not included** in this repository.

---

## ⚠️ Important for BYTE-90 Device Owners

**This firmware is not compatible with Series 1 BYTE 90 devices. Series 2 owners will require a Series 2 AI ready PCB upgrade kit**.
Flashing custom firmware to a purchased BYTE-90 device may:

- Cause loss of proprietary animations and visual effects stored in flash  
- Create incompatibility with specific hardware revisions  
- Lead to boot issues or device malfunction  

Use official pre-built firmware releases for commercial devices unless you are comfortable with embedded firmware development.

---

## 🚀 Firmware Features

**Designed BYTE-90 Series 2 AI Edition** BYTE 90 Firmware 3.0 enables:

- Interactive animations using a state-driven GIF system  
- Motion detection (tap, double-tap, shake, tilt, orientation)  
- Retro screen effects (scanlines, dot-matrix, glitch, tint colors)  
- AI chatbot integration (Xiaozhi AI / Tenclass pipeline)  
- MCP tool invocation framework for device and API integrations  
- Clock mode with NTP sync and timezone support  
- AI activated timers
- Captive portal for Wi‑Fi setup and device settings  
- Intelligent sleep and power management  
- Web Serial firmware updater  
- On-device audio capture and streaming pipeline  

---

## 🧠 System Architecture Overview

| Subsystem | Responsibility |
|----------|----------------|
| Display Engine | GIF rendering and screen effects |
| Motion Engine | Accelerometer gesture detection |
| Audio Pipeline | Microphone capture → encode → AI stream |
| AI Layer | Xiaozhi backend (Tenclass pipeline) |
| Device Core | State machine, sleep logic, and settings |
| MCP Tools | Device and API tool execution |

---

## 🛠 Getting Started (Developers)

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- Python 3.7+  
- Git  
- USB-C cable  

### Target Board Configuration

```ini
[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
build_flags = 
	-DCORE_DEBUG_LEVEL=1
	-DBOARD_NAME=\"BYTE-90\"
	-DFIRMWARE_VERSION=\"3.0.0\"
board_build.partitions = partitions.csv
board_build.filesystem = littlefs
lib_ldf_mode = deep+
```

### Required Libraries
```ini
lib_deps = 
    lewisxhe/XPowersLib@^0.3.1                     # Power / AXP2101
    bblanchon/ArduinoJson@^7.4.2                   # JSON
    sh123/esp32_opus@^1.0.3                        # Opus codec
    adafruit/Adafruit GFX Library@^1.11.11         # Graphics
    adafruit/Adafruit SSD1351 library@^1.3.2       # Display driver
    adafruit/Adafruit DRV2605 Library@^1.2.4       # Haptics
    adafruit/Adafruit ADXL345@^1.3.4               # Accelerometer
    adafruit/RTClib@^2.1.4                         # RTC
    bitbank2/AnimatedGIF@^2.2.0                    # GIF decoding
    arduino-libraries/ArduinoMqttClient@^0.1.8     # MQTT transport
```

---

## 🤖 AI Services

For AI functionality you must either:

- Activate your device via Xiaozhi AI [Xiaozhi.me](https://xiaozhi.me/)  

OpenAI support is not included in this branch.
For OpenAI firmware support, switch to the OpenAI-specific firmware branch.
The OpenAI-specific branch requires your own OpenAI API key.

Service availability, pricing, and policies may change.

---

## 🤖 OpenAI Integration (openai-api branch)

OpenAI support lives on the `openai-api` branch and is not included here.

To use it:
1. Switch branches: `git checkout openai-api`
2. Configure your OpenAI API key: connect to the Configuration Portal and enter your key.
3. OpenAI’s API is pay‑per‑use.
4. The firmware uses OpenAI’s Realtime API for the realtime agent model.
5. Build and flash as usual.

---

## 🎞 Animation Assets

This open-source firmware **does not include animations**.

Commercial BYTE-90 devices include proprietary animations. DIY builders must provide their own optimized GIFs:

- Resolution: 128×128 pixels
- 8-bit indexed color GIF (256 colors max)
- 16 FPS recommended  
- Optimized GIF with LZW compression [Use EZgif.com](https://ezgif.com/)

---

## 🔋 Device Safety

### Battery Safety
- ⚠️ **Check connector polarity alignment carefully**
- ⚠️ DO NOT connect battery into the speaker connector
- Use ONLY the specified 3.7V lithium battery (103040 size) with PH 2.0 connector
- Verify 3.7V voltage (NOT 3.9V) before installation
- Handle battery connector with care to avoid damage

### Environmental Safety
**BYTE-90 is designed as a desktop device for indoor use only.**
- ⚠️ Do NOT use in vehicles or automotive environments - Heat, temperature fluctuations, and vibrations in cars can cause device overheating, battery damage, or malfunction.
- ⚠️ Avoid high-temperature environments - Operating temperature should remain below 35°C (95°F) to prevent overheating.
- ⚠️ Keep away from direct sunlight - Prolonged sun exposure can cause overheating and display damage.

---

## 📚 Documentation

See the `/docs` directory for:

- [AUDIO_PROCESSING.md](docs/AUDIO_PROCESSING.md)
- [AUDIO_STREAMING.md](docs/AUDIO_STREAMING.md)
- [API_REFERENCE.md](docs/API_REFERENCE.md)
- [BUTTON_INTERACTION.md](docs/BUTTON_INTERACTION.md)
- [CODING_STYLE_GUIDE.md](docs/CODING_STYLE_GUIDE.md)
- [EMOJI_SYSTEM.md](docs/EMOJI_SYSTEM.md)
- [LANGUAGE_SYSTEM.md](docs/LANGUAGE_SYSTEM.md)
- [MCP_TOOLS_GUIDE.md](docs/MCP_TOOLS_GUIDE.md)
- [MEMORY_AND_CORE_ARCHITECTURE.md](docs/MEMORY_AND_CORE_ARCHITECTURE.md)
- [OPENAI_WEBSOCKET_API_FLOW.md](docs/OPENAI_WEBSOCKET_API_FLOW.md)
- [PROJECT_STRUCTURE_OVERVIEW.md](docs/PROJECT_STRUCTURE_OVERVIEW.md)
- [XIAOZHI_PROVISIONING_API.md](docs/XIAOZHI_PROVISIONING_API.md)
- [XIAOZHI_MCPTOOL_API.md](docs/XIAOZHI_MCPTOOL_API.md)
- [XIAOZHI_MQTTUDP_API_FLOW.md](docs/XIAOZHI_MQTTUDP_API_FLOW.md)
- [XIAOZHI_ROLE.md](docs/XIAOZHI_ROLE.md)
- [XIAOZHI_WEBSOCKET_API_FLOW.md](docs/XIAOZHI_WEBSOCKET_API_FLOW.md)

---

## 🧯 Troubleshooting

### Common Issues

**Device Not Responding**
- **Charge first**: New devices need 1-2 hours initial charging
- **Reset**: Press "Reset" button on Xiao ESP32-S3 board
- **USB power**: Try direct USB-C power without battery

**Random Restarts or Freeze**
- **Cause**: Critically low battery
- **Solution**: Charge immediately for 1-2 hours
- **Hardware Reset**: Press reset button on board

## Frequently Asked Questions

**Q: How long does the battery last?**
Up to 2 days with intelligent power management and progressive sleep modes.

**Q: How do I turn off BYTE-90?**
Use the power button (long press) to request shutdown. The device also uses automatic sleep modes. For a hard power-off, disconnect the battery.

**Q: Can I modify the animations or sensitivity?**
Requires programming knowledge. Animations remain proprietary, but motion thresholds can be adjusted in firmware.

**Q: Can I create commercial products using this firmware?**
Yes, under GPL v3.0 terms, but you **cannot use BYTE-90 branding or any BYTE-90 proprietary assets including original designs, 3D printed models, and animations** See [Legal Guidelines](CONTRIBUTING.md) for details.

**Q: What's the difference between the open source firmware and commercial version?**
- Open source includes core functionality
- Commercial version includes full hardware and proprietary animations with device support and exclusive access to new feature releases.

---

## Acknowledgements
- **Xiazhi**: [Xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- **Adafruit Industries**: Hardware libraries and sensor drivers
- **Bitbank2**: AnimatedGIF library for efficient GIF rendering
- **Espressif Systems**: ESP32 development framework and tools
- **Seeedstudio**: XIAO ESP32S3 development board design
- **Community Contributors**: Open source development and testing

## 🤝 Contributing & Legal

Firmware is licensed under **GPL v3.0**.

BYTE-90 branding, animations, visual identity, and 3D models are proprietary assets of ALXV Labs.

Full legal terms, contribution rules, branding restrictions, and commercial-use policies: **see `CONTRIBUTING.md`** [CONTRIBUTING.md](CONTRIBUTING.md)

---

*Designed and developed by Alex Vong, ALXV LABS. This project represents the intersection of retro computing nostalgia and modern interactive design, creating a unique interactive designer toy experience for makers and collectors alike.*
