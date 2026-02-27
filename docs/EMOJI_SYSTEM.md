# Emoji/Emotion System Documentation

**Version**: 3.1 (Updated for current GIF-based emotions pipeline)
**Last Updated**: 2026-02-02

This document describes the emoji/emotion system used in the byte90-xiaozhi device, including how the server sends emotion data, how the device processes and displays it as animated GIFs, and the supported emotion values.

---

## Table of Contents

1. [Overview](#overview)
2. [Server Message Formats](#server-message-formats)
3. [Supported Emotions](#supported-emotions)
4. [Display Behavior](#display-behavior)
5. [Implementation Details](#implementation-details)
6. [Code Locations](#code-locations)
7. [Examples](#examples)
---

## Overview

The device supports displaying emotions/emojis based on server messages during conversation. The emotion system:

- Receives emotion data from the server via WebSocket or MQTT protocols
- Displays emotions as **animated GIFs** using the same state GIF naming system
- Plays the emotion GIF once, then resumes the previously playing state
- Plays immediately (no waiting for a speaking cycle)
- Updates asynchronously to avoid blocking protocol callbacks
- Supports any emotion name that has a matching GIF file on disk

### Current Implementation (Byte-90)

The Byte-90 firmware displays emotions as **animated GIFs**:
- **Display Format**: Animated GIF files (128×128 pixels)
- **Location**: `/gifs/state_<emotion>.gif` or `/gifs/state_<emotion>_*.gif`
- **Playback**: Play once (non-looping), then resume the prior state GIF
- **Transition**: Immediate (no wait for speaking cycle)
- **Priority**: Emotion GIFs temporarily override the current state
- **Update**: Real-time as emotions are received from server

---

## Server Message Formats

The server can send emotions in JSON messages during conversation:

### LLM (Emotion/Expression) Message

**Purpose:** Update device emotion/expression display during conversation

**Message Format:**
```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "llm",
  "emotion": "happy",
  "text": "I'm glad to help you!"
}
```

**Field Descriptions:**

| Field | Type | Required | Description |
|------|------|----------|-------------|
| `session_id` | string | Yes | Session identifier |
| `type` | string | Yes | Message type, must be `"llm"` |
| `emotion` | string | Yes | Emotion name (e.g., `"happy"`, `"sad"`, `"neutral"`) |
| `text` | string | Optional | Response text (for context) |

**When Received:**
- During active conversation sessions
- May be sent multiple times during a session
- Updates the device UI expression in real-time

**Code Locations:**
- [`src/ProtocolManager.cpp:433-441`](src/ProtocolManager.cpp#L433-L441)
- [`src/Application.cpp:452-460`](src/Application.cpp#L452-L460)

**Processing:**
```cpp
else if (strcmp(typeStr, "llm") == 0) {
    const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
    if (emotion && cJSON_IsString(emotion)) {
        ESP_LOGI(TAG, "📱 LLM Response - Emotion received: '%s'", emotion->valuestring);
        if (_emotion_callback) {
            _emotion_callback(String(emotion->valuestring));
        }
    }
}
```

---

## Supported Emotions

The server can send any emotion name. The device will display it as a GIF **if a matching file exists**:

### Example Emotion List

| Emotion Name | Description | Example Use Case |
|--------------|-------------|------------------|
| `neutral` | Neutral expression | Default state, casual conversation |
| `happy` | Happy/smiling | Positive responses, success |
| `laughing` | Laughing | Jokes, funny situations |
| `funny` | Funny/amused | Humorous responses |
| `sad` | Sad | Unfortunate news, empathy |
| `angry` | Angry | Strong disagreement (rare) |
| `crying` | Crying | Very sad situations |
| `loving` | Loving/affectionate | Warm, caring responses |
| `embarrassed` | Embarrassed | Mistakes, awkward moments |
| `surprised` | Surprised | Unexpected information |
| `shocked` | Shocked | Very surprising news |
| `thinking` | Thinking/contemplating | Processing, considering |
| `winking` | Winking | Playful, secretive |
| `cool` | Cool/confident | Impressive facts, achievements |
| `relaxed` | Relaxed | Calm, peaceful topics |
| `delicious` | Delicious/tasty | Food-related discussions |
| `kissy` | Kissy face | Affectionate goodbye |
| `confident` | Confident | Assured responses |
| `sleepy` | Sleepy/tired | Late night, tiredness |
| `silly` | Silly | Playful, goofy responses |
| `confused` | Confused | Unclear input, questions |

**Note**: There is no text fallback; if no matching GIF exists, the emotion is skipped.

---

## Display Behavior

### Emotion Display Process

1. **Receive Message**: Protocol handler receives JSON message with `emotion` field
2. **Parse Emotion**: Extract emotion string from JSON
3. **Queue Emotion**: Call `ApplicationUI::playEmotionOnce()` with emotion name
4. **Play Emotion GIF**: Load `/gifs/state_<emotion>.gif` (or variation) and play once
5. **Return to Prior State**: After emotion GIF completes, resume the previous state
6. **Log**: Console logs emotion for debugging

### Emotion Playback Flow

**Files:** [`src/ApplicationUI.cpp:700-720`](src/ApplicationUI.cpp#L700-L720), [`lib/gif_player/GifManager.cpp:240-295`](lib/gif_player/GifManager.cpp#L240-L295)

```
Current State GIF Playing
    ↓
Server sends emotion "happy"
    ↓
playEmotionOnce("happy") called
    ↓
Store resume state name
Set _emotion_active = true
    ↓
Load /gifs/state_happy.gif (or state_happy_*.gif)
Play once (non-looping)
    ↓
Emotion GIF plays to completion
    ↓
Detect completion via takeFinishedOnce()
    ↓
Clear _emotion_active
    ↓
Return to previous state (or idle)
```

### Display Specifications

**GIF Properties:**
- **Resolution**: 128×128 pixels (full screen)
- **Format**: Animated GIF with transparency support
- **Location**: `/gifs/state_<emotion>.gif` or `/gifs/state_<emotion>_*.gif`
- **Playback**: Non-looping (plays once)
- **Priority**: Temporarily overrides the current state GIF

**State Flags:**

```cpp
// In GifManager class
bool _emotion_active;               // True while emotion GIF is playing
String _resume_state_name;          // State to resume after emotion finishes
```

### Timing Considerations

Emotions are played immediately and do not wait for a speaking cycle. The GIF player signals completion using `takeFinishedOnce()` and the manager resumes the previous state.

---

## Implementation Details

### File Structure

```
src/
├── Application.cpp                 # Emotion callback wiring
├── ApplicationUI.cpp              # Emotion playback entry point
└── ProtocolManager.cpp            # Emotion parsing from server messages

lib/gif_player/
├── GifManager.h                   # Emotion playback interface
└── GifManager.cpp                 # Emotion GIF playback logic
```

### Key Classes and Methods

#### ApplicationUI Class

**Header**: [`src/ApplicationUI.h`](src/ApplicationUI.h)

```cpp
class ApplicationUI {
public:
  void playEmotionOnce(const String& emotion);
};
```

**Implementation**: [`src/ApplicationUI.cpp`](src/ApplicationUI.cpp)

```cpp
void ApplicationUI::playEmotionOnce(const String& emotion)
{
    if (_showing_activation || _showing_clock || !_gif_manager) {
        return;
    }
    _gif_manager->playEmotionOnce(emotion);
}
```

#### GifManager Class

**Header**: [`lib/gif_player/GifManager.h`](lib/gif_player/GifManager.h)

```cpp
class GifManager {
public:
  void playEmotionOnce(const String& emotion_name);
};
```

**Implementation**: [`lib/gif_player/GifManager.cpp`](lib/gif_player/GifManager.cpp)

```cpp
void GifManager::playEmotionOnce(const String& emotion_name) {
    if (!_player || emotion_name.length() == 0) {
        return;
    }

    if (!_state_discovered[emotion_name]) {
        discoverStateGifs(emotion_name);
    }

    // Choose a matching GIF, play once, then resume
    _resume_state_name = _current_state_name;
    _emotion_active = true;
    _player->requestGIF(gif_path.c_str(), false);
}
```

---

## Code Locations

### Core Implementation Files

| File | Purpose |
|------|---------|
| [`src/ProtocolManager.cpp`](src/ProtocolManager.cpp) | Parses LLM messages and extracts emotions |
| [`src/Application.cpp`](src/Application.cpp) | Wires protocol emotion callback to UI |
| [`src/ApplicationUI.cpp`](src/ApplicationUI.cpp) | Entry point for emotion playback |
| [`lib/gif_player/GifManager.cpp`](lib/gif_player/GifManager.cpp) | Emotion GIF playback logic |

### Key Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `ProtocolManager::handleIncomingJson()` | `src/ProtocolManager.cpp` | Parse LLM messages and extract emotions |
| `ApplicationUI::playEmotionOnce()` | `src/ApplicationUI.cpp` | Dispatch emotion playback to GIF manager |
| `GifManager::playEmotionOnce()` | `lib/gif_player/GifManager.cpp` | Play emotion GIF once and resume prior state |

---

## Examples

### Example 1: Basic Emotion Display

**Server sends:**
```json
{
  "session_id": "abc123",
  "type": "llm",
  "emotion": "happy",
  "text": "I'm glad to help you!"
}
```

**Device behavior:**
1. Parses JSON message
2. Extracts `"happy"` emotion
3. Calls `playEmotionOnce("happy")`
4. Plays `/gifs/state_happy.gif` (or a variation)
5. Returns to prior state when finished

### Example 2: Missing Emotion GIF

**Server sends:**
```json
{"type": "llm", "emotion": "ultra_happy"}
```

**Device behavior:**
- No matching `/gifs/state_ultra_happy.gif` (or variation)
- Emotion is skipped and the current state continues

