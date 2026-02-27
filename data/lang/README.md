# Language Assets Directory - Byte90 Xiaozhi

This directory contains language-specific assets for the runtime language system using **LittleFS** filesystem.

## Platform

- **Device:** Byte90 Xiaozhi (ESP32-S3)
- **Framework:** PlatformIO/Arduino
- **Filesystem:** LittleFS (3MB assets partition)
- **Supported Languages:** en-US, zh-CN

## Structure

```
lang/
├── en-US/
│   ├── language.json    # English translations (34 strings)
│   └── sounds/          # English audio files (OGG) - optional
│       ├── welcome.ogg
│       ├── goodbye.ogg
│       └── ...
└── zh-CN/
    ├── language.json    # Chinese translations (34 strings)
    └── sounds/          # Chinese audio files (OGG) - optional
        ├── welcome.ogg
        ├── goodbye.ogg
        └── ...
```

## File Format

### language.json Structure

```json
{
  "language": {
    "type": "en-US"
  },
  "strings": {
    "WARNING": "Warning",
    "INFO": "Information",
    "ERROR": "Error",
    "LISTENING": "Listening...",
    "SPEAKING": "Speaking..."
  }
}
```

**Important Notes:**
- Filename is `language.json` (not `strings.json`)
- Root object contains two keys: `language` and `strings`
- `language.type` specifies the locale
- `strings` object contains key-value translation pairs
- All keys are UPPERCASE with underscores

## Current Strings

The Byte90 Xiaozhi implementation includes **34 optimized strings** (reduced from 51 in original firmware):

### Categories

- **Status & System:** `INITIALIZING`, `STANDBY`, `CONNECTING`, `LISTENING`, `SPEAKING`, `PLEASE_WAIT`
- **Errors:** `ERROR`, `WARNING`, `INFO`
- **Network:** `SERVER_NOT_FOUND`, `SERVER_NOT_CONNECTED`, `SERVER_TIMEOUT`, `SERVER_ERROR`, `CONNECT_TO`, `CONNECTED_TO`, `CONNECTION_SUCCESSFUL`
- **WiFi Config:** `WIFI_CONFIG_MODE`, `ENTERING_WIFI_CONFIG_MODE`, `SCANNING_WIFI`, `CONNECT_TO_HOTSPOT`, `ACCESS_VIA_BROWSER`
- **Activation:** `ACTIVATION`, `LOADING_PROTOCOL`
- **Battery:** `BATTERY_LOW`, `BATTERY_CHARGING`, `BATTERY_FULL`, `BATTERY_NEED_CHARGE`
- **Audio:** `VOLUME`, `MUTED`, `MAX_VOLUME`, `RTC_MODE_OFF`, `RTC_MODE_ON`
- **Version:** `VERSION`
- **Greeting:** `HELLO_MY_FRIEND`

### Removed Strings

The following strings from the original ESP-IDF firmware were removed as they are not applicable:

- Cellular/4G network strings (WiFi-only device)
- OTA firmware update strings (not implemented)
- Asset download strings (assets bundled in LittleFS)

See [LANGUAGE_STRING_USAGE_DOCUMENTATION.md](../../docs/LANGUAGE_STRING_USAGE_DOCUMENTATION.md) for complete details.

## Adding/Editing Strings

### 1. Add to Both Language Files

Edit both `en-US/language.json` and `zh-CN/language.json`:

```json
{
  "language": {
    "type": "en-US"
  },
  "strings": {
    "EXISTING_KEY": "Existing value",
    "NEW_KEY": "New translated string"
  }
}
```

### 2. Naming Convention

- Use `UPPERCASE_WITH_UNDERSCORES`
- Be descriptive and concise
- Group by category: `BATTERY_*`, `SERVER_*`, `WIFI_*`

### 3. Display Constraints

- OLED display: 128x128 pixels
- Maximum line length: ~16-20 characters (depends on font)
- Test Chinese characters for proper rendering

## Adding Audio Files

### Format Requirements

- **Container:** OGG Vorbis (`.ogg`)
- **Sample Rate:** 16kHz or 24kHz
- **Channels:** Mono (recommended) or Stereo
- **Bitrate:** 64-96 kbps (good quality, small size)
- **Naming:** Match string keys (lowercase with underscores)

### Recommended Audio Files

```
sounds/
├── welcome.ogg          # Welcome greeting
├── goodbye.ogg          # Goodbye message
├── listening.ogg        # "I'm listening" prompt
├── speaking.ogg         # "Speaking" notification
├── error.ogg            # Error sound
├── battery_low.ogg      # Low battery warning
├── connected.ogg        # WiFi/server connected
└── activation.ogg       # Device activation sound
```

### Creating Audio Files

Using FFmpeg to convert:

```bash
# Convert to OGG Vorbis (16kHz mono)
ffmpeg -i input.wav -ar 16000 -ac 1 -c:a libvorbis -q:a 4 output.ogg

# Convert to OGG Vorbis (24kHz mono, higher quality)
ffmpeg -i input.wav -ar 24000 -ac 1 -c:a libvorbis -q:a 5 output.ogg
```

### File Size Guidelines

- Short prompts (1-2 seconds): ~5-20KB
- Medium phrases (3-5 seconds): ~20-40KB
- Longer messages (5-10 seconds): ~40-80KB
- **Target:** Keep total per language under 1MB

## Upload to Device

After adding/modifying files, upload the filesystem to the ESP32:

```bash
# Upload data directory to LittleFS partition
pio run --target uploadfs
```

This command:
1. Builds filesystem image from `data/` directory
2. Uploads to the 3MB LittleFS assets partition
3. Preserves existing data if partition is large enough

**Note:** This will overwrite the existing LittleFS partition content.

## Testing

### Verify Language Files

After uploading, monitor serial output during boot:

```
I (1234) LanguageManager: Loading language: en-US
I (1235) LanguageManager: ✓ Loaded 34 strings for en-US
```

### Test String Access

```cpp
// In your code
LanguageManager& lang = LanguageManager::getInstance();

// Check if ready
if (lang.isReady()) {
    // Get string
    const char* status = lang.getString("LISTENING");
    Serial.println(status);  // Output: "Listening..."
}

// Check sound file
if (lang.hasSoundFile("welcome")) {
    String path = lang.getSoundPath("welcome");
    Serial.println(path);  // Output: "/assets/lang/en-US/sounds/welcome.ogg"
}
```

### Debug Information

Print complete language manager status:

```cpp
lang.printDebugInfo();
```

Output:
```
I (1234) LanguageManager: === Language Manager Debug Info ===
I (1235) LanguageManager: Filesystem mounted: Yes
I (1236) LanguageManager: Manager ready: Yes
I (1237) LanguageManager: Current language: en-US
I (1238) LanguageManager: Strings loaded: 34
I (1239) LanguageManager: Sounds directory: /assets/lang/en-US/sounds
I (1240) LanguageManager: Available languages:
I (1241) LanguageManager:   - en-US
I (1242) LanguageManager:   - zh-CN
I (1243) LanguageManager: ===================================
```

### Verify Filesystem

Check LittleFS contents:

```cpp
LittleFSManager& fs = LittleFSManager::getInstance();

// Print directory tree
fs.printDirectoryTree("/assets");

// Get storage stats
StorageStats stats = fs.getStorageStats();
Serial.printf("Used: %.2f MB / %.2f MB (%.1f%%)\n",
              stats.usedBytes / (1024.0 * 1024.0),
              stats.totalBytes / (1024.0 * 1024.0),
              stats.usedPercent);
```

## Runtime Language Switching

Change language at runtime:

```cpp
LanguageManager& lang = LanguageManager::getInstance();

// Switch to Chinese
if (lang.setLanguage("zh-CN")) {
    Serial.println("Language switched to Chinese");

    // Save preference to NVS
    nvs.setString("language", "zh-CN");
}

// Get current language
const char* current = lang.getCurrentLanguage();
Serial.println(current);  // Output: "zh-CN"
```

## Troubleshooting

### "Language file not found" Error

**Cause:** `language.json` missing or wrong path

**Solution:**
1. Verify filename is `language.json` (not `strings.json`)
2. Check path: `/assets/lang/en-US/language.json`
3. Re-upload filesystem: `pio run --target uploadfs`

### "Failed to parse language file" Error

**Cause:** Invalid JSON format

**Solution:**
1. Validate JSON syntax online (jsonlint.com)
2. Check for missing commas, quotes, or brackets
3. Ensure UTF-8 encoding for Chinese characters

### Strings Not Displaying Correctly

**Cause:** Wrong key or missing translation

**Solution:**
1. Check key spelling (case-sensitive)
2. Verify key exists in both language files
3. Use `lang.printDebugInfo()` to see loaded strings

### Audio Files Not Playing

**Cause:** Wrong format or missing file

**Solution:**
1. Verify OGG Vorbis format (not MP3/WAV)
2. Check filename matches key (lowercase)
3. Use `lang.hasSoundFile("key")` to verify
4. Check path: `/assets/lang/en-US/sounds/welcome.ogg`

## Storage Management

### Check Available Space

```cpp
LittleFSManager& fs = LittleFSManager::getInstance();
size_t free = fs.getFreeBytes();
Serial.printf("Free space: %.2f MB\n", free / (1024.0 * 1024.0));
```

### Partition Size

- **Total:** 3MB (3,145,728 bytes)
- **Current usage:** ~2-3KB (JSON files only)
- **Available for audio:** ~2.99MB

### Optimize Audio Files

If running low on space:
1. Reduce sample rate (16kHz instead of 24kHz)
2. Lower bitrate (`-q:a 3` instead of `-q:a 5`)
3. Trim silence from beginning/end
4. Use shorter messages

## Related Documentation

- [Language System Implementation](../../docs/LANGUAGE_SYSTEM_IMPLEMENTATION.md) - Architecture overview
- [Language String Usage](../../docs/LANGUAGE_STRING_USAGE_DOCUMENTATION.md) - Complete string reference
- [LittleFS Manager](../../docs/LITTLEFS_MANAGER.md) - Filesystem abstraction
- [Language System Usage](../../docs/LANGUAGE_SYSTEM_USAGE.md) - Developer guide

## Example Workflow

### Adding a New String

1. **Edit JSON files:**
   ```json
   // en-US/language.json
   "NEW_FEATURE_NAME": "New Feature"

   // zh-CN/language.json
   "NEW_FEATURE_NAME": "新功能"
   ```

2. **Upload filesystem:**
   ```bash
   pio run --target uploadfs
   ```

3. **Use in code:**
   ```cpp
   const char* text = lang.getString("NEW_FEATURE_NAME");
   display.showStatus(text);
   ```

### Adding Audio File

1. **Convert audio:**
   ```bash
   ffmpeg -i new_sound.wav -ar 16000 -ac 1 -c:a libvorbis -q:a 4 sounds/new_sound.ogg
   ```

2. **Copy to both languages:**
   ```bash
   cp sounds/new_sound.ogg en-US/sounds/
   cp sounds/new_sound.ogg zh-CN/sounds/
   ```

3. **Upload and test:**
   ```bash
   pio run --target uploadfs
   ```

4. **Play in code:**
   ```cpp
   String path = lang.getSoundPath("new_sound");
   if (!path.isEmpty()) {
       audioService.playFile(path.c_str());
   }
   ```

## Version History

- **v1.0.0** - Initial Byte90 Xiaozhi implementation
  - 34 optimized strings
  - LittleFS-based storage
  - Runtime language switching
  - Support for localized audio files

---

**Last Updated:** 2025-01-26
