# JSON Objects Extracted from Logs

## 1. Serial Interface Ready
```json
{
  "success": true,
  "message": "BYTE-90 Serial Interface Ready",
  "ssid": "",
  "rssi": 0,
  "signal_strength": "NOT_CONNECTED",
  "connected": false,
  "state": "UPDATE_MODE",
  "system_mode": "Update Mode",
  "networks": null
}
```

## 2. Provisioning Request Payload (Client → Server)

Captured from live OTA check logs.
- Endpoint: `https://api.tenclass.net/xiaozhi/ota/`
- Log tag: `[Provisioning] Request payload`

```json
{
  "version": 2,
  "language": "en-US",
  "flash_size": 8388608,
  "psram_size": 8385239,
  "minimum_free_heap_size": 156080,
  "mac_address": "1c:db:d4:75:88:cc",
  "uuid": "792bf676-bed1-4fae-8a7d-3bbc4f5d3e92",
  "chip_model_name": "ESP32-S3",
  "chip_info": {
    "model": 9,
    "cores": 2,
    "revision": 0,
    "features": 18
  },
  "application": {
    "name": "arduino-lib-builder",
    "version": "esp-idf: v4.4.7 38eeba213a",
    "compile_time": "Mar  5 2024T12:12:53Z",
    "idf_version": "v4.4.7-dirty",
    "elf_sha256": "619a2d46fda6f41043b91148093bd8c51a8af2eed4928a0ff885b02d99561b69"
  },
  "partition_table": [
    {
      "label": "nvs",
      "type": 1,
      "subtype": 2,
      "address": 36864,
      "size": 16384
    },
    {
      "label": "otadata",
      "type": 1,
      "subtype": 0,
      "address": 53248,
      "size": 8192
    },
    {
      "label": "phy_init",
      "type": 1,
      "subtype": 1,
      "address": 61440,
      "size": 4096
    },
    {
      "label": "ota_0",
      "type": 0,
      "subtype": 16,
      "address": 65536,
      "size": 2555904
    },
    {
      "label": "ota_1",
      "type": 0,
      "subtype": 17,
      "address": 2621440,
      "size": 2621440
    },
    {
      "label": "assets",
      "type": 1,
      "subtype": 130,
      "address": 5242880,
      "size": 3145728
    }
  ],
  "ota": {
    "label": "ota_0"
  },
  "display": {
    "monochrome": false,
    "width": 128,
    "height": 128
  },
  "board": {
    "type": "BYTE-90",
    "name": "BYTE-90",
    "ssid": "2cats",
    "rssi": -39,
    "channel": 1,
    "ip": "192.168.86.26",
    "mac": "1c:db:d4:75:88:cc"
  },
  "firmware_version": "3.1.0"
}
```

## 3. Provisioning Response Payload (Server → Client)

Captured from live OTA check logs.
- Endpoint: `https://api.tenclass.net/xiaozhi/ota/`
- Log tag: `[Provisioning] Response body`

```json
{
  "mqtt": {
    "endpoint": "mqtt.xiaozhi.me",
    "client_id": "GID_test@@@1c_db_d4_75_88_cc@@@792bf676-bed1-4fae-8a7d-3bbc4f5d3e92",
    "username": "eyJpcCI6IjEzNC4yMzEuNTYuODkifQ==",
    "password": "CEURb4S/Klmqml/9AULYPu9ScIg41plR3RMTvZ3v2DE=",
    "publish_topic": "device-server",
    "subscribe_topic": "null"
  },
  "websocket": {
    "url": "wss://api.tenclass.net/xiaozhi/v1/",
    "token": "test-token"
  },
  "server_time": {
    "timestamp": 1770499372521,
    "timezone_offset": -300
  },
  "firmware": {
    "version": "esp-idf: v4.4.7 38eeba213a",
    "url": ""
  }
}
```

## 4. WebSocket Hello (Client → Server)
```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "features": {
    "mcp": true,
    "aec": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

## 5. WebSocket Hello (Server → Client)
```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  },
  "session_id": "04134d65"
}
```

## 6. MQTT Hello (Client → Server, UDP Transport)
```json
{
  "type": "hello",
  "version": 3,
  "transport": "udp",
  "features": {
    "mcp": true,
    "aec": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

## 7. MQTT Hello (Server → Client, UDP Details)
```json
{
  "type": "hello",
  "version": 3,
  "session_id": "b9e1d68e",
  "transport": "udp",
  "udp": {
    "server": "47.76.65.170",
    "port": 8885,
    "encryption": "aes-128-ctr",
    "key": "40711a81786b35f5f127fb31c32841b8",
    "nonce": "010000007014e57e0000000000000000"
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

**Note:** MQTT control messages are exchanged over MQTT topics (device-server and per-device p2p topics). Audio streams over UDP after the server hello is received.

## 8. MCP Initialize Request
```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "initialize",
    "id": 1,
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {
        "vision": {
          "url": "http://api.xiaozhi.me/vision/explain",
          "token": "04134d65-941b-422f-b368-018413ba49a8"
        }
      },
      "clientInfo": {
        "name": "xiaozhi-mqtt-client",
        "version": "1.0.0"
      }
    }
  },
  "session_id": "04134d65"
}
```

## 9. MCP Initialize Response
```json
{
  "type": "mcp",
  "session_id": "04134d65",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "protocolVersion": "2024-11-05",
      "capabilities": {
        "tools": {}
      },
      "serverInfo": {
        "name": "BYTE-90",
        "version": "3.1.0"
      }
    }
  }
}
```

## 10. MCP Initialized Notification
```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "notifications/initialized"
  },
  "session_id": "04134d65"
}
```

## 11. MCP Tools List Request
```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 2,
    "params": {}
  },
  "session_id": "04134d65"
}
```

## 12. MCP Tools List Response (Page 1)
```json
{
  "type": "mcp",
  "session_id": "04134d65",
  "payload": {
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        {
          "name": "self.get_weather",
          "description": "Fetch current weather for a location using Google Geocoding and Weather APIs.\nUse this tool for:\n1. Answering current weather questions.\n2. Fetching temperature, conditions, humidity, and wind details.\nNotes: provide location or set it with self.set_location. Units are c/f.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "location": {
                "type": "string",
                "default": ""
              },
              "unit": {
                "type": "string",
                "default": "c"
              }
            }
          }
        },
        {
          "name": "self.describe_self",
          "description": "Provide a self-description payload for the assistant.\nUse this tool for:\n1. Answering questions like 'who are you' or 'describe yourself'.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.tell_joke",
          "description": "Guide the assistant to tell 3 jokes themed around retro computing, development, Byte 90, retro gaming, and microcontrollers.\nUse this tool for:\n1. When the user asks for jokes or a funny response.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.get_device_status",
          "description": "Provide real-time device status including audio, display, network, and timer info.\nUse this tool for:\n1. Answering questions about current device state (volume, brightness, network).\n2. As a first step before changing device settings.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.get_time",
          "description": "Get current device time (syncs via NTP) for a timezone.\nUse this tool for:\n1. Answering time questions for a specific timezone.\n2. Ensuring time is synced before time-based features.\nNotes: timezone_name is required unless set with self.set_timezone.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "timezone_name": {
                "type": "string",
                "description": "User timezone. Use stored timezone from NVS to get the local time."
              }
            },
            "required": ["timezone_name"]
          }
        },
        {
          "name": "self.set_timezone",
          "description": "Set the default timezone_name for MCP tools and persist it to storage.\nUse this tool for:\n1. Saving a user's preferred timezone for future time requests.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "timezone_name": {
                "type": "string"
              }
            },
            "required": ["timezone_name"]
          }
        },
        {
          "name": "self.set_location",
          "description": "Set the default location for MCP tools and persist it to storage.\nUse this tool for:\n1. Saving a user's default location for weather queries.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "location": {
                "type": "string"
              }
            },
            "required": ["location"]
          }
        },
        {
          "name": "self.audio_speaker.set_volume",
          "description": "Set the volume of the audio speaker (0-100).\nUse this tool for:\n1. Changing speaker volume when the user asks to raise/lower volume.\n2. Applying a specific volume level after checking current status with self.get_device_status.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "volume": {
                "type": "integer",
                "minimum": 0,
                "maximum": 100
              }
            },
            "required": ["volume"]
          }
        },
        {
          "name": "self.display.set_brightness",
          "description": "Set the display brightness (0-100).\nUse this tool for:\n1. Adjusting screen brightness on user request.\n2. Applying a specific brightness after checking current status with self.get_device_status.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "brightness": {
                "type": "integer",
                "minimum": 0,
                "maximum": 100
              }
            },
            "required": ["brightness"]
          }
        },
        {
          "name": "self.display.show_clock",
          "description": "Display a digital clock on screen using the provided timezone and read out the current time.\nUse this tool for:\n1. Entering clock mode when the user asks to show the clock/time on screen.\n2. Showing the time in a specific timezone (set or provided).\nNotes: call self.get_device_status first to confirm whether clock mode is already active.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "timezone_name": {
                "type": "string",
                "default": ""
              }
            }
          }
        },
        {
          "name": "self.display.stop_clock",
          "description": "Clear the clock display and resume GIF animations.\nUse this tool for:\n1. Turning off clock mode when the user asks to hide or stop the clock.\nNotes: call self.get_device_status first to confirm whether clock mode is active.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_scanlines",
          "description": "Enable scanline effects on the display (retro CRT look).\nUse this tool for:\n1. Turning scanlines on when the user asks for scanline effect.\n2. Applying scanlines as part of a retro visual request.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.disable_scanlines",
          "description": "Disable scanline effects on the display.\nUse this tool for:\n1. Turning scanlines off when the user asks to disable them.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_glitch",
          "description": "Enable glitch effects on the display.\nUse this tool for:\n1. Turning on glitch effect when the user asks for glitch visuals.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.disable_glitch",
          "description": "Disable glitch effects on the display.\nUse this tool for:\n1. Turning off glitch effect when the user asks to disable it.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_tint_green",
          "description": "Enable a green tint effect on the display (overrides any existing tint).\nUse this tool for:\n1. Turning on green tint when the user asks to switch to green color.\n2. Applying a green/terminal-style tint on request.\nNotes: call self.get_device_status first to confirm current tint.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_tint_blue",
          "description": "Enable a blue tint effect on the display (overrides any existing tint).\nUse this tool for:\n1. Turning on blue tint when the user asks to switch to blue color.\n2. Applying a blue screen tint on request.\nNotes: call self.get_device_status first to confirm current tint.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_tint_yellow",
          "description": "Enable a yellow tint effect on the display (overrides any existing tint).\nUse this tool for:\n1. Turning on yellow tint when the user asks to switch to yellow color.\n2. Applying a warm/amber tint on request.\nNotes: call self.get_device_status first to confirm current tint.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.disable_tint",
          "description": "Disable the tint effect on the display.\nUse this tool for:\n1. Removing any active tint when the user asks to clear tints.\n2. Switching back to normal/white colors when the user asks to reset colors.\nNotes: call self.get_device_status first to confirm current tint.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.enable_dot_matrix",
          "description": "Enable dot matrix effect on the display.\nUse this tool for:\n1. Turning on dot matrix effect when the user asks for it.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.disable_dot_matrix",
          "description": "Disable dot matrix effect on the display.\nUse this tool for:\n1. Turning off dot matrix effect when the user asks to disable it.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.display_effects.disable_all",
          "description": "Disable all display effects (scanlines, glitch, dot matrix, and tints).\nUse this tool for:\n1. Clearing all active effects when the user asks to reset visuals.\n2. Removing all effects when the user asks to clear or remove effects.\nNotes: call self.get_device_status first to confirm current effects state.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        }
      ],
      "nextCursor": "self.timer.set"
    }
  }
}
```

## 13. MCP Tools List Request (Page 2)
```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 3,
    "params": {
      "cursor": "self.timer.set"
    }
  },
  "session_id": "04134d65"
}
```

## 14. MCP Tools List Response (Page 2)
```json
{
  "type": "mcp",
  "session_id": "04134d65",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "result": {
      "tools": [
        {
          "name": "self.timer.list",
          "description": "List all active timers.\nUse this tool for:\n1. Answering 'what timers do I have running?' or before canceling a specific timer.",
          "inputSchema": {
            "type": "object",
            "properties": {}
          }
        },
        {
          "name": "self.timer.status",
          "description": "Get the status of a specific timer (or the soonest-expiring one if no id given).\nUse this tool for:\n1. Answering questions about whether a timer is running and time remaining.\nNotes: omit id or pass 0 to query the soonest-expiring running timer.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "id": { "type": "integer", "minimum": 0, "maximum": 255 }
            }
          }
        },
        {
          "name": "self.timer.cancel",
          "description": "Cancel a timer by id, or the most-recently started timer if no id is given.\nUse this tool for:\n1. Stopping a running timer when the user asks to cancel it.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "id": { "type": "integer", "minimum": 0, "maximum": 255 }
            }
          }
        },
        {
          "name": "self.timer.repeat",
          "description": "Repeat (restart) a timer by id, or the most-recently started timer if no id is given.\nUse this tool for:\n1. Restarting a timer when the user asks to do it again.",
          "inputSchema": {
            "type": "object",
            "properties": {
              "id": { "type": "integer", "minimum": 0, "maximum": 255 }
            }
          }
        }
      ]
    }
  }
}
```

## 15. Listen Start Command
```json
{
  "type": "listen",
  "session_id": "04134d65",
  "state": "start",
  "mode": "auto"
}
```

## 16. Listen Detect Command (Synthetic Wake Word)
```json
{
  "type": "listen",
  "session_id": "04134d65",
  "state": "detect",
  "text": "What's up"
}
```

**Note:** This is a synthetic wake word detection message used to prompt an initial greeting. The current firmware does not implement real wake word detection.

## 17. TTS Start Event
```json
{
  "type": "tts",
  "state": "start",
  "sample_rate": 24000,
  "session_id": "04134d65"
}
```

## 18. STT (Speech-to-Text) Result
```json
{
  "type": "stt",
  "text": "Hello.",
  "session_id": "04134d65"
}
```

## 19. LLM Response with Emotion
```json
{
  "type": "llm",
  "text": "😎",
  "emotion": "cool",
  "session_id": "04134d65"
}
```

## 20. TTS Sentence Start
```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "Hello!",
  "session_id": "04134d65"
}
```

## 21. TTS Sentence End
```json
{
  "type": "tts",
  "state": "sentence_end",
  "text": "Hello!",
  "session_id": "04134d65"
}
```

## 22. TTS Sentence Start (Second Sentence)
```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "Ready to dive into some retro vibes?",
  "session_id": "04134d65"
}
```

## 23. TTS Sentence End (Second Sentence)
```json
{
  "type": "tts",
  "state": "sentence_end",
  "text": "Ready to dive into some retro vibes?",
  "session_id": "04134d65"
}
```

## 24. TTS Stop Event
```json
{
  "type": "tts",
  "state": "stop",
  "session_id": "04134d65"
}
```

## 25. Abort Command
```json
{
  "type": "abort",
  "session_id": "04134d65",
  "reason": "user_stopped"
}
```

---

## Summary

This log contains **22 distinct JSON objects** representing various protocol messages in a WebSocket-based voice assistant system (BYTE-90). The messages follow this flow:

### 1. Device Provisioning (Startup)
- Device sends hardware/firmware info to server
- Server responds with connection credentials and configuration

### 2. WebSocket Connection
- Client and server exchange "hello" messages with capabilities

### 3. MCP Tool Registration
- Server initializes MCP protocol
- Device registers 25 available tools (weather, device control, display effects, timers)

### 4. Voice Interaction
- Audio streaming (listen, TTS, STT)
- LLM responses with emotion metadata
- Session control (abort)

The device is an ESP32-S3 based retro-style voice assistant with:
- 128x128 color display
- Display effects (scanlines, glitch, tint, dot matrix)
- Haptic feedback
- Audio codec with Opus encoding/decoding
- MCP tools for weather, time, display control, and timer functionality
- OTA firmware update capability
