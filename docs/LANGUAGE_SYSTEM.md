# Language System Documentation

## Overview

The Language System provides **runtime language switching** with support for localized strings and audio files. The system uses the 3MB assets partition on **LittleFS** for storing language files and persists language preferences across reboots.

**Implementation Status**: ✅ COMPLETE

### Features Implemented

- ✅ Runtime language switching (en-US, zh-CN)
- ✅ JSON-based string translations
- ✅ Localized audio file support (OGG format)
- ✅ Language preference persistence (NVS storage)
- ✅ LittleFS-based asset storage (3MB partition)
- ✅ Power-loss protection and wear leveling
- ✅ Fallback to default language
- ✅ String key fallback for missing translations
- ✅ Easy language expansion architecture

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────┐
│                     Application                         │
│  ┌────────────────┐         ┌────────────────────────┐  │
│  │ NVSStorage     │         │  LanguageManager       │  │
│  │                │         │                        │  │
│  │ - Save/Load    │◄───────►│  - Load strings.json   │  │
│  │   language     │         │  - getString()         │  │
│  │   preference   │         │  - getSoundPath()      │  │
│  └────────────────┘         │  - setLanguage()       │  │
│                             └────────────────────────┘  │
│                                       │                 │
│                                       ▼                 │
│                             ┌────────────────────────┐  │
│                             │   LittleFS Assets      │  │
│                             │  /assets/lang/         │  │
│                             │    ├── en-US/          │  │
│                             │    │   ├── strings.json│  │
│                             │    │   └── sounds/     │  │
│                             │    └── zh-CN/          │  │
│                             │        ├── strings.json│  │
│                             │        └── sounds/     │  │
│                             └────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### Flash Management Integration

The Language System incorporates robust flash management patterns for production-ready error handling, fast boot optimization, and deferred validation.

#### Status Code Pattern

```cpp
enum class LangStatus {
    LANG_SUCCESS,
    LANG_FS_NOT_MOUNTED,
    LANG_FILE_NOT_FOUND,
    LANG_PARSE_ERROR,
    LANG_INVALID_LANGUAGE
};
```

#### Fast Boot Optimization

Separates critical file checks from full validation:

```cpp
// Fast: Only checks default language (en-US) files
bool checkCriticalFiles();

// Deferred: Validates all language files (can be called after boot)
bool validateLanguageFiles();
```

**Boot Sequence**:
```
1. Mount LittleFS (if needed)
2. checkCriticalFiles() - Fast check for en-US
   ├─ /assets/lang/en-US/ exists?
   └─ /assets/lang/en-US/strings.json exists?
3. Load default language
4. [Boot complete - fast!]
5. validateLanguageFiles() - Full check (optional, deferred)
```

---

## File Structure

### LittleFS Structure

```
/assets/lang/
├── en-US/
│   ├── strings.json          # English translations
│   └── sounds/
│       ├── welcome.ogg       # Audio files (optional)
│       ├── goodbye.ogg
│       └── ...
└── zh-CN/
    ├── strings.json          # Chinese translations
    └── sounds/
        ├── welcome.ogg       # Audio files (optional)
        ├── goodbye.ogg
        └── ...
```

### Language File Format

#### strings.json

```json
{
  "language": {
    "type": "en-US"
  },
  "strings": {
    "INITIALIZING": "Initializing...",
    "STANDBY": "Standby",
    "LISTENING": "Listening...",
    "SPEAKING": "Speaking...",
    "ERROR": "Error",
    "WARNING": "Warning"
  }
}
```

**Format Rules**:
- Root object contains `language` and `strings` keys
- `language.type` specifies the locale (e.g., "en-US", "zh-CN")
- `strings` object contains key-value translation pairs
- Keys should be descriptive identifiers (ALL_CAPS with underscores)
- Values are the localized strings
- Keep keys consistent across all languages

#### Audio Files

- **Format**: OGG Vorbis (`.ogg`)
- **Location**: `/assets/lang/{language}/sounds/{sound_name}.ogg`
- **Naming**: Use descriptive names matching string keys where applicable
- **Sample Rate**: 16kHz recommended
- **Channels**: Mono

---

## API Reference

### Core Methods

```cpp
class LanguageManager {
public:
    // Initialization (enhanced with status codes)
    LangStatus begin();
    bool loadLanguage(const char* lang_code);

    // String access
    const char* getString(const char* key);
    const char* getCurrentLanguage() const;

    // Audio access
    String getSoundPath(const char* sound_name);
    bool hasSoundFile(const char* sound_name);

    // Language management
    bool setLanguage(const char* lang_code);
    const char** getAvailableLanguages(size_t& count);

    // Validation (fast boot optimization pattern)
    bool checkCriticalFiles();  // Fast check for essential files
    bool validateLanguageFiles();  // Full validation (deferred)

    // Utility
    bool isReady() const;
    void printDebugInfo();
};
```

### NVS Integration

```cpp
// From NVSStorage class
bool setLanguage(const char* language);
String getLanguage();
```

---

## Usage Examples

### Basic Usage

```cpp
#include "language_manager.h"

// Get instance (usually from Application)
LanguageManager* lang = _language_manager;

// Get translated string
const char* welcome = lang->getString("welcome");
Serial.println(welcome);  // Prints: "Welcome" or "欢迎" depending on language

// Get audio file path
String sound_path = lang->getSoundPath("welcome");
// Returns: "/assets/lang/en-US/sounds/welcome.ogg"

// Check if sound exists
if (lang->hasSoundFile("welcome")) {
    // Play audio file
}
```

### Runtime Language Switching

```cpp
#include "language_manager.h"
#include "nvs_storage.h"

// Switch to Chinese
if (_language_manager->setLanguage("zh-CN")) {
    // Save preference for next boot
    _storage->setLanguage("zh-CN");

    ESP_LOGI(TAG, "Language changed to Chinese");

    // Update UI with new strings
    updateUI();
}

// Switch to English
if (_language_manager->setLanguage("en-US")) {
    _storage->setLanguage("en-US");
    ESP_LOGI(TAG, "Language changed to English");
    updateUI();
}
```

### Available Languages

```cpp
size_t count;
const char** languages = _language_manager->getAvailableLanguages(count);

Serial.printf("Available languages: %d\n", count);
for (size_t i = 0; i < count; i++) {
    Serial.printf("  - %s\n", languages[i]);
}

// Output:
// Available languages: 2
//   - en-US
//   - zh-CN
```

### Boot-time Language Loading

```cpp
void Application::initializeLanguage() {
    ESP_LOGI(TAG, "4.5. Language Manager...");
    _language_manager = new LanguageManager();

    // Filesystem already mounted by Application
    LangStatus status = _language_manager->begin();

    switch (status) {
        case LangStatus::LANG_SUCCESS:
            ESP_LOGI(TAG, "✓ Language initialized");
            break;

        case LangStatus::LANG_FILE_NOT_FOUND:
            ESP_LOGE(TAG, "Language files missing");
            ESP_LOGW(TAG, "Please run: pio run --target uploadfs");
            return;

        // ... handle other errors
    }

    // Load saved language preference
    if (_storage) {
        String saved_lang = _storage->getLanguage();
        if (!saved_lang.isEmpty() && saved_lang.length() > 0) {
            ESP_LOGI(TAG, "Loading saved language: %s", saved_lang.c_str());
            if (_language_manager->setLanguage(saved_lang.c_str())) {
                ESP_LOGI(TAG, "✓ Language set to: %s", saved_lang.c_str());
            } else {
                ESP_LOGW(TAG, "Failed to load saved language, using default");
            }
        } else {
            ESP_LOGI(TAG, "No saved language, using default: en-US");
        }
    }
}
```

### UI Text Display

```cpp
void updateStatusDisplay() {
    if (!_language_manager || !_language_manager->isReady()) {
        return;
    }

    // Get localized strings
    const char* status = _language_manager->getString("STANDBY");
    const char* wifi = _language_manager->getString("CONNECTED_TO");

    // Update display
    display.println(status);
    display.println(wifi);
}
```

### Audio Prompts

```cpp
void playWelcomeMessage() {
    if (!_language_manager) {
        return;
    }

    // Get localized audio path
    String audio_path = _language_manager->getSoundPath("welcome");

    if (!audio_path.isEmpty()) {
        // Play audio file
        _audio_service->playFile(audio_path.c_str());
    } else {
        ESP_LOGW(TAG, "Welcome audio not found for current language");
    }
}
```

### Adding New Languages

To add a new language (e.g., Spanish):

1. **Update LanguageManager.cpp**:
```cpp
const char* LanguageManager::AVAILABLE_LANGUAGES[] = {"en-US", "zh-CN", "es-ES"};
const size_t LanguageManager::LANGUAGE_COUNT = 3;
```

2. **Create Language Files**:
```bash
mkdir -p data/lang/es-ES/sounds
# Create data/lang/es-ES/strings.json
# Add Spanish audio files to data/lang/es-ES/sounds/
```

3. **Upload to LittleFS**:
```bash
pio run --target uploadfs
```

4. **Use the new language**:
```cpp
_language_manager->setLanguage("es-ES");
_storage->setLanguage("es-ES");
```

---

## String Reference

### Total Strings: 34 (optimized from original 51)

The Byte90 Xiaozhi implementation includes **34 optimized strings** (reduced from 51 in original firmware):

### Categories

#### Status & System Strings (6)
- `INITIALIZING` - "Initializing..." / "正在初始化..."
- `STANDBY` - "Standby" / "待机"
- `CONNECTING` - "Connecting..." / "正在连接..."
- `LISTENING` - "Listening..." / "正在听..."
- `SPEAKING` - "Speaking..." / "正在说话..."
- `PLEASE_WAIT` - "Please wait..." / "请稍候..."

#### Error & Warning Strings (3)
- `ERROR` - "Error" / "错误"
- `WARNING` - "Warning" / "警告" (reserved)
- `INFO` - "Information" / "信息" (reserved)

#### Network & Server Strings (7)
- `SERVER_NOT_FOUND` - "Looking for available service" / "正在寻找可用服务"
- `SERVER_NOT_CONNECTED` - "Unable to connect to service, please try again later" / "无法连接到服务，请稍后再试"
- `SERVER_TIMEOUT` - "Waiting for response timeout" / "等待响应超时"
- `SERVER_ERROR` - "Sending failed, please check the network" / "发送失败，请检查网络"
- `CONNECT_TO` - "Connect to " / "连接到 "
- `CONNECTED_TO` - "Connected to " / "已连接到 "
- `CONNECTION_SUCCESSFUL` - "Connection Successful" / "连接成功" (reserved)

#### WiFi Configuration Strings (5)
- `WIFI_CONFIG_MODE` - "Wi-Fi Configuration Mode" / "Wi-Fi配置模式"
- `ENTERING_WIFI_CONFIG_MODE` - "Entering Wi-Fi configuration mode..." / "正在进入Wi-Fi配置模式..."
- `SCANNING_WIFI` - "Scanning Wi-Fi..." / "正在扫描Wi-Fi..."
- `CONNECT_TO_HOTSPOT` - "Hotspot: " / "热点: "
- `ACCESS_VIA_BROWSER` - " Config URL: " / " 配置网址: "

#### Device Activation Strings (2)
- `ACTIVATION` - "Activation" / "激活"
- `LOADING_PROTOCOL` - "Logging in..." / "正在登录..."

#### Battery Strings (4)
- `BATTERY_LOW` - "Low battery" / "电量低" (reserved)
- `BATTERY_CHARGING` - "Charging" / "充电中" (reserved)
- `BATTERY_FULL` - "Battery full" / "电量充足" (reserved)
- `BATTERY_NEED_CHARGE` - "Low battery, please charge" / "电量低，请充电"

#### Audio & Volume Strings (5)
- `VOLUME` - "Volume " / "音量 " (reserved)
- `MUTED` - "Muted" / "已静音" (reserved)
- `MAX_VOLUME` - "Max volume" / "最大音量" (reserved)
- `RTC_MODE_OFF` - "AEC Off" / "回声消除关闭"
- `RTC_MODE_ON` - "AEC On" / "回声消除开启"

#### Version String (1)
- `VERSION` - "Ver " / "版本 "

#### Greeting String (1)
- `HELLO_MY_FRIEND` - "Hello, my friend!" / "你好，我的朋友！" (reserved)

### Removed Strings

The following strings from the original ESP-IDF firmware have been removed as they are not applicable to our implementation:

- **Cellular/4G Network (4 strings)**: WiFi-only device
- **Network Switching (2 strings)**: No dual network capability
- **OTA Firmware (3 strings)**: No firmware OTA implemented
- **OTA Upgrade Alerts (3 strings)**: No firmware OTA implemented
- **Asset Download (3 strings)**: Assets bundled in LittleFS

### Usage Statistics

| Category | Total | Active | Reserved | Implementation |
|----------|-------|--------|----------|----------------|
| Status & System | 6 | 6 | 0 | 100% |
| Error & Warning | 3 | 1 | 2 | 33% |
| Network & Server | 7 | 6 | 1 | 86% |
| WiFi Configuration | 5 | 5 | 0 | 100% |
| Device Activation | 2 | 2 | 0 | 100% |
| Battery | 4 | 1 | 3 | 25% |
| Audio & Volume | 5 | 2 | 3 | 40% |
| Version | 1 | 1 | 0 | 100% |
| Greeting | 1 | 0 | 1 | 0% |
| **TOTAL** | **34** | **24** | **10** | **71%** |

---

## Debugging

### Debug Output

```cpp
_language_manager->printDebugInfo();
```

**Output**:
```
I (12345) LanguageManager: === Language Manager Debug Info ===
I (12346) LanguageManager: Filesystem mounted: Yes
I (12347) LanguageManager: Manager ready: Yes
I (12348) LanguageManager: Current language: zh-CN
I (12349) LanguageManager: Strings loaded: 28
I (12350) LanguageManager: Sounds directory: /assets/lang/zh-CN/sounds
I (12351) LanguageManager: Available languages:
I (12352) LanguageManager:   - en-US
I (12353) LanguageManager:   - zh-CN
I (12354) LanguageManager: ===================================
```

### Common Issues

#### 1. Language File Not Found

**Error**: `Language file not found: /assets/lang/zh-CN/strings.json`

**Solution**:
- Verify LittleFS is mounted
- Check file exists with correct path
- Upload assets partition: `pio run --target uploadfs`

#### 2. Sound File Not Found

**Warning**: `Sound file not found: /assets/lang/en-US/sounds/welcome.ogg`

**Solution**:
- Check audio file exists in correct directory
- Verify file extension is `.ogg`
- Ensure file was uploaded to LittleFS

#### 3. JSON Parse Error

**Error**: `Failed to parse language file: InvalidInput`

**Solution**:
- Validate JSON syntax (use jsonlint.com)
- Check for trailing commas
- Ensure UTF-8 encoding

#### 4. Filesystem Not Mounted

**Error**: `LangStatus::LANG_FS_NOT_MOUNTED`

**Solution**:
- Ensure LittleFSManager is initialized before LanguageManager
- Check partition table configuration
- Verify filesystem partition exists

---

## Performance Notes

- **Memory**: Strings loaded into RAM (std::map)
- **String Lookup**: O(log n) complexity
- **Language Switch**: ~100-200ms (depends on file size)
- **LittleFS Access**: Cached after first read with wear leveling
- **Typical Memory Usage**: ~2-4KB per 50 strings
- **Boot Time Impact**: +10-20ms (critical check only)

---

## Best Practices

1. **Key Naming**:
   - Use descriptive ALL_CAPS keys with underscores
   - Group related strings with prefixes (e.g., `ERROR_`, `BATTERY_`, `SERVER_`)
   - Keep keys consistent across all languages

2. **String Content**:
   - Keep strings concise for display constraints
   - Test with longest language (usually German/Chinese)
   - Use placeholders for dynamic content where needed

3. **Audio Files**:
   - Use consistent naming with string keys
   - Optimize file sizes (16kHz mono is sufficient)
   - Test audio duration matches expected use case

4. **Language Switching**:
   - Always save preference to NVS
   - Update all UI elements after switch
   - Provide visual/audio feedback

5. **Testing**:
   - Test all languages before release
   - Verify all audio files exist
   - Check for missing translations

---

## Initialization Sequence

```
1. Application::start()
   └─> 2. initializeStorage()
       └─> 3. NVSStorage::begin()
   └─> 4. initializeLanguage()
       └─> 5. LanguageManager::begin()
           └─> 6. Load default language (en-US)
       └─> 7. Load saved preference from NVS
           └─> 8. setLanguage() if preference exists
               └─> 9. loadLanguage() from LittleFS
                   └─> 10. Parse strings.json from LittleFS
```

---

## Memory Usage

### Current Build Stats
- **RAM**: 14.6% (47,768 bytes / 327,680 bytes)
- **Flash**: 62.9% (1,607,705 bytes / 2,555,904 bytes)

### Language System Impact
- LanguageManager class: ~100 bytes
- String map overhead: ~2-4KB (for 34 strings)
- Per string: ~50-100 bytes (key + value + map node)

### LittleFS Assets Partition
- Total: 3MB (3,145,728 bytes)
- Current usage: ~2KB (JSON files only)
- Available for audio: ~3MB
- Features: Power-loss protection, wear leveling, directory support

---

## Known Limitations

1. **Static Language List**: Languages must be defined at compile time in AVAILABLE_LANGUAGES array
2. **Full String Loading**: All strings loaded into RAM (not on-demand)
3. **No String Formatting**: No printf-style placeholders yet
4. **No Plural Rules**: Single form for all quantities
5. **No Context**: Same key can't have different translations based on context

---

## Future Enhancements

Potential future additions:

1. **Dynamic String Loading**: Load strings on-demand to reduce memory
2. **Plural Forms**: Support for plural rules per language
3. **String Formatting**: Printf-style placeholders for dynamic content
4. **Language Auto-Detection**: Based on WiFi region or user preference
5. **OTA Language Updates**: Download new languages from server
6. **Translation Fallback Chain**: en-US → zh-CN → key (multi-level fallback)

---

## Source Code Reference

- **Implementation**: `lib/language/language_manager.cpp` and `lib/language/language_manager.h`
- **Integration**: `src/application.cpp::initializeLanguage()`
- **Storage**: `lib/storage/nvs_storage.cpp` (language preference)
- **Filesystem**: `lib/filesystem/littlefs_manager.cpp` (LittleFS access)

---

## Related Documentation

- [Partitions Documentation](../04-device-management/PARTITIONS.md) - Assets partition configuration and filesystem
- [Device Startup Sequence](../01-getting-started/DEVICE_STARTUP_SEQUENCE.md) - Language manager initialization
- [Code Structure and Flow](../01-getting-started/CODE_STRUCTURE_AND_FLOW.md) - Overall architecture
- [MQTT NVS Storage Usage](../04-device-management/MQTT_NVS_STORAGE_USAGE.md) - Configuration storage patterns

---

**Version:** 2.1.0  
**Last Updated:** 2025-01-25

## Changelog

### Version 2.1.0 (2025-01-25)
- Updated documentation format to match other protocol documents
- Added comprehensive changelog section
- Updated related documentation links
- Documented runtime language switching (en-US, zh-CN)
- Added JSON-based string translation system
- Documented localized audio file support (OGG format)
- Added language preference persistence via NVS
- Documented LittleFS-based asset storage (3MB partition)
- Added flash management integration with status codes
- Documented fast boot optimization pattern
- Added complete string reference (34 optimized strings)
- Documented removed strings from original ESP-IDF firmware
- Added usage statistics and implementation percentages

### Version 1.0 (Initial)
- Basic language system implementation
- Support for en-US and zh-CN languages
- JSON-based string translations
- NVS storage for language preference
- LittleFS integration for asset storage

