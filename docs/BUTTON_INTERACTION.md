# Button Interaction and Device States Documentation

This document provides documentation for button interaction flow, protocol connection lifecycle, and device state management in the Byte-90 firmware.

**Last Updated:** 2026-02-11
**Cross-referenced with:** `src/Application.cpp`, `src/InputManager.cpp`, `src/ProtocolManager.cpp`, `src/ApplicationServices.cpp`, `src/ApplicationAudio.cpp`, `lib/power/Axp2101.cpp`, `lib/system/SystemState.h`, `lib/adxl/AdxlManager.h`

---

## Table of Contents

1. [Overview](#overview)
2. [AXP2101 Configuration](#axp2101-configuration)
3. [System States](#system-states)
4. [Button Initialization](#button-initialization)
5. [Button Logic Decision Tree](#button-logic-decision-tree)
6. [Scenario 1: First Button Press (Connect and Listen)](#scenario-1-first-button-press-connect-and-listen)
7. [Scenario 2: Second Button Press (Stop Listening)](#scenario-2-second-button-press-stop-listening)
8. [Scenario 3: Button Press During TTS Playback](#scenario-3-button-press-during-tts-playback)
9. [Scenario 4: Already Connected (Immediate Listen)](#scenario-4-already-connected-immediate-listen)
10. [Scenario 5: Clock Mode Button Press](#scenario-5-clock-mode-button-press)
11. [Scenario 6: Light Sleep Wake](#scenario-6-light-sleep-wake)
12. [State Transition Rules](#state-transition-rules)
13. [On-Demand Connection Pattern](#on-demand-connection-pattern)
14. [Protocol Connection Lifecycle](#protocol-connection-lifecycle)
15. [JSON Protocol Messages](#json-protocol-messages)
16. [Timing Analysis](#timing-analysis)
---

## Overview

The Byte-90 device uses a **button-triggered, on-demand connection model** where the user presses a physical button to initiate conversations with the AI server. The button serves multiple purposes depending on the current device state and protocol mode (WebSocket, MQTT, or OpenAI Realtime):

- **When idle (not connected)**: Connect protocol and start listening
- **When listening**: Stop listening and disconnect
- **When speaking (TTS)**: Interrupt playback (stay connected)
- **When clock mode is active and not connected**: Clear clock display
- **When in light sleep**: Wake device
- **Long press**: Initiate shutdown sequence

**Hardware note:** The button is tied to the AXP2101 PWRON key, not a standard GPIO.
Button events come from AXP2101 IRQ status reads and are surfaced via
`AXP2101::updateButton()` to `InputManager`, which publishes `EventBus` button events.

## AXP2101 Configuration

The button is routed through the AXP2101 power key, and Byte-90 only wires **DC1**. The initialization in `lib/power/Axp2101.cpp` is required for reliable button events and stable power to audio hardware.

Key behaviors:

- **DC1 only:** DC2/DC3/DC4/DC5 and all LDOs are disabled.
- **Power key IRQs:** Only power key IRQs are enabled (`SHORT`, `LONG`, `POSITIVE`).
- **Power key timing:** On-time and long-press thresholds are configured via AXP2101.
- **IRQ clearing:** IRQ status is cleared after reads to avoid stale events.

The device implements a state machine that manages transitions between different operational states, ensuring smooth user experience and efficient resource usage.

### Key Design Principles

1. **On-Demand Connection**: Protocol connects only when needed (button press)
2. **Single Button Control**: One button controls the entire conversation flow
3. **Visual Feedback**: Screen animations reflect current state (idle, connecting, listening, speaking)
4. **Audio Feedback**: MP3 sounds play on connection/disconnection
5. **State-Driven Behavior**: All components respond to centralized state changes

---

## System States

The device uses a state machine defined in `lib/system/SystemState.h` to track its operational status.

### State Enumeration

```cpp
enum SystemState {
    SYSTEM_STATE_UNKNOWN = 0,          // Initial/unknown state
    SYSTEM_STATE_STARTING = 1,         // Device booting up
    SYSTEM_STATE_WIFI_CONFIGURING = 2, // WiFi setup mode
    SYSTEM_STATE_IDLE = 3,             // Standby, waiting for input (DEFAULT)
    SYSTEM_STATE_CONNECTING = 4,       // Connecting to server
    SYSTEM_STATE_LISTENING = 5,        // Recording audio
    SYSTEM_STATE_SPEAKING = 6,         // Playing TTS audio
    SYSTEM_STATE_LOADING = 7,          // Fetching remote data
    SYSTEM_STATE_ACTIVATING = 8,       // Device activation
};
```

### State Descriptions

#### SYSTEM_STATE_IDLE (Default State)

**Purpose:** Device is powered on, configured, and ready for user interaction.

**Characteristics:**
- Protocol may be connected or disconnected
- Audio capture/playback stopped
- Waiting for button press

**Entry Points:**
- After boot and initialization
- After protocol connection completes
- After stopping listening
- After TTS playback completes (in auto mode)
- After disconnection

#### SYSTEM_STATE_CONNECTING

**Purpose:** Device is establishing a protocol connection to server.

**Characteristics:**
- Protocol handshake in progress
- Cannot start listening yet

**Entry Points:**
- Button pressed when not connected
- Automatic reconnection after WiFi recovery

**Exit Points:**
- Server hello received → IDLE (connected, ready)
- Connection failure → IDLE (disconnected)

#### SYSTEM_STATE_LISTENING

**Purpose:** Device is actively recording and streaming audio to server.

**Characteristics:**
- Microphone capturing audio
- Audio streaming to server

**Entry Points:**
- After connection completes (auto-triggered by pending flag)
- Button pressed when already connected

**Exit Points:**
- Button pressed (user stops) → Disconnect → IDLE
- Server sends TTS start → SPEAKING
- Protocol disconnect → IDLE

#### SYSTEM_STATE_SPEAKING

**Purpose:** Server is sending TTS audio, device is playing it back.

**Characteristics:**
- Audio playback active
- Microphone capture stopped
- Button press interrupts playback

**Entry Points:**
- Server sends TTS start message during listening

**Exit Points:**
- TTS stop received (auto mode) → Listen restart → LISTENING
- TTS stop received (manual mode) → IDLE
- Button pressed (interrupt) → Cancel/abort response
- Protocol disconnect → IDLE

#### SYSTEM_STATE_STARTING

**Purpose:** Device is booting up and initializing hardware.

**Exit Points:**
- Initialization complete → IDLE

#### SYSTEM_STATE_WIFI_CONFIGURING

**Purpose:** Device is in WiFi setup mode (access point).

**Exit Points:**
- WiFi credentials saved → Restart → STARTING

#### SYSTEM_STATE_ACTIVATING

**Purpose:** Device is performing initial activation with server.

---

## Button Initialization

Button events are wired in `InputManager::initializeButton()`:

- AXP2101 click → `EventType::BUTTON_CLICK`
- AXP2101 long press → `EventType::BUTTON_LONG_PRESS`

The `InputManager` subscribes to these events and runs the decision tree below.

---

## Button Logic Decision Tree

**Source:** `src/InputManager.cpp`

High-level logic order:

1. **Light sleep check**: if in light sleep, wake and return.
2. **Ignore-after-wake guard**: skip click if just woke from sleep.
3. **Play button click haptic** if enabled.
4. **Clock mode check**: if clock showing and device is idle, clear clock if not connected.
5. **Speaking check**: if speaking, abort/cancel response and return.
6. **Listening check**:
   - If not listening:
     - If not connected → start connect sequence and queue listening.
     - If connected → start listening immediately.
   - If listening → stop listening.

---

## Scenario 1: First Button Press (Connect and Listen)

**Trigger:** Device idle and not connected.

**Flow:**
- Set state to `SYSTEM_STATE_CONNECTING`
- Play connecting sound (throttled)
- Schedule `connect_protocol` task
- Set `_pending_listening_start = true`

---

## Scenario 2: Second Button Press (Stop Listening)

**Trigger:** Device is currently listening.

**Flow:**
- Publish `EventType::STOP_LISTENING`
- Protocol manager handles disconnect/stop

---

## Scenario 3: Button Press During TTS Playback

**Trigger:** Device is speaking.

**Flow:**
- OpenAI realtime: publish `CANCEL_OPENAI_RESPONSE`
- Otherwise: publish `ABORT_RESPONSE`
- Play interrupt sound with haptic (if not already playing)

---

## Scenario 4: Already Connected (Immediate Listen)

**Trigger:** Device idle, connected, not listening.

**Flow:**
- Publish `EventType::START_LISTENING`

---

## Scenario 5: Clock Mode Button Press

**Trigger:** Clock display active and system state is IDLE.

**Flow:**
- If not connected, clear clock and return.
- If connected, fall through to normal button logic.

---

## Scenario 6: Light Sleep Wake

**Trigger:** Motion state indicates `LIGHT_SLEEP`.

**Flow:**
- Wake from sleep via `AdxlManager::wakeFromSleep()`
- Return without starting any protocol action

---

## State Transition Rules

- Button click can move state from IDLE → CONNECTING.
- TTS start moves LISTENING → SPEAKING.
- TTS stop may resume LISTENING (auto) or return to IDLE.
- Disconnect always returns to IDLE.

---

## On-Demand Connection Pattern

- Button press triggers connection on demand.
- `_pending_listening_start` ensures listening begins once connected.
- Connection task is scheduled via TaskManager to avoid blocking UI.

---

## Protocol Connection Lifecycle

1. Button click requests connect.
2. `ProtocolManager` opens the protocol session.
3. Server sends hello.
4. Listening starts when ready (if pending).
5. TTS start/stop drive speaking transitions.

---

## JSON Protocol Messages

- `{"type":"stt"}`: speech-to-text inbound
- `{"type":"tts","state":"start|stop"}`: text-to-speech transitions
- `{"type":"llm","emotion":"..."}`: emotion updates

---

## Timing Analysis

- Connecting sound is throttled to avoid overlap.
- Input handling avoids blocking the UI task.
- Motion and sleep guards run before any protocol actions.
