/**
 * @file GifManager.cpp
 * @brief Implementation of GIF file discovery and randomization
 * 
 * Uses LittleFSManager for filesystem access with dynamic directory scanning.
 */

#include "GifManager.h"
#include "../storage/LittlefsManager.h"
#include "ArduinoSSD1351.h"
#include "DeviceConfig.h"
#include <esp_log.h>
#include <esp_random.h>

static const char* TAG = "GifManager";

GifManager::GifManager()
    : _player(nullptr)
    , _fs(nullptr)
    , _current_state_name("")
    , _last_played_path("")
    , _last_randomize_ms(0)
    , _emotion_active(false)
    , _resume_state_name("")
    , _idle_gifs_built(false)
    , _idle_emote_playing(false)
    , _idle_base_gif("")
    , _last_idle_emote_path("")
    , _idle_emote_gifs()
    , _emotion_states()
    , _emotion_states_built(false)
    , _last_emotion_state("")
{
  // Constructor
}

GifManager::~GifManager() {
  if (_player) {
    delete _player;
    _player = nullptr;
  }
}

bool GifManager::begin(LittleFSManager* fs, ArduinoSSD1351* display, SemaphoreHandle_t display_mutex) {
  _fs = fs;
  // Initialize GifPlayer
  if (!_player) {
      _player = new GifPlayer(display);
      if (_player) {
          _player->setDisplayMutex(display_mutex);
          if (!_player->begin()) {
              ESP_LOGE(TAG, "Failed to initialize GifPlayer");
              return false;
          }
      } else {
          ESP_LOGE(TAG, "Failed to allocate GifPlayer");
          return false;
      }
  }

  ESP_LOGD(TAG, "GifManager initialized");
  return true;
}

bool GifManager::hasGifs(LittleFSManager* fs) {
  if (!fs) return false;
  if (!fs->exists(GIF_DIR)) {
      return false;
  }
  
  File dir = fs->open(GIF_DIR);
  if (!dir || !dir.isDirectory()) {
      return false;
  }
  
  File file = dir.openNextFile();
  while (file) {
      String filename = String(file.name());
      if (filename.endsWith(".gif")) {
          file.close();
          dir.close();
          return true;
      }
      file.close();
      file = dir.openNextFile();
  }
  dir.close();
  return false;
}

void GifManager::buildIdleGifs() {
  if (_idle_gifs_built) return;
  if (!_state_discovered["idle"]) {
      discoverStateGifs("idle");
  }

  _idle_emote_gifs.clear();
  _idle_base_gif = "";

  String base_path = String(GIF_DIR) + "/state_idle.gif";
  if (_fs && _fs->exists(base_path.c_str())) {
      _idle_base_gif = base_path;
  }

  auto it = _state_gifs_cache.find("idle");
  if (it != _state_gifs_cache.end()) {
      const std::vector<String>& gifs = it->second;
      if (_idle_base_gif.length() == 0 && !gifs.empty()) {
          _idle_base_gif = gifs[0];
      }
      for (const auto& path : gifs) {
          if (path != _idle_base_gif) {
              _idle_emote_gifs.push_back(path);
          }
      }
  }

  _idle_gifs_built = true;
}

void GifManager::playIdleBase() {
  if (!_player || _idle_base_gif.length() == 0) return;
  _idle_emote_playing = false;
  _player->requestGIF(_idle_base_gif.c_str(), true);
}

void GifManager::playIdleEmote() {
  if (!_player || _idle_emote_gifs.empty()) return;

  String selected_gif;
  int attempts = 0;
  do {
      uint32_t random_value = esp_random();
      int random_index = random_value % _idle_emote_gifs.size();
      selected_gif = _idle_emote_gifs[random_index];
      attempts++;
  } while (selected_gif == _last_idle_emote_path && attempts < 5);

  _last_idle_emote_path = selected_gif;
  _idle_emote_playing = true;
  _player->requestGIF(selected_gif.c_str(), false);
}

void GifManager::update() {
    if (!_player) return;

    if (_emotion_active) {
        if (_player->takeFinishedOnce()) {
            _emotion_active = false;
            String resume_state = _resume_state_name;
            _resume_state_name = "";
            if (resume_state.length() > 0) {
                playState(resume_state);
            } else {
                playState("idle");
            }
        }
        return;
    }

    if (_current_state_name == "listening" || _current_state_name == "speaking") {
        if (hasVariations(_current_state_name) && GIF_INTERVAL_MS > 0) {
            uint32_t now = millis();
            bool loop_completed = _player->takeFinishedOnce();
            if ((uint32_t)(now - _last_randomize_ms) >= (uint32_t)GIF_INTERVAL_MS &&
                loop_completed) {
                ESP_LOGD(TAG, "GIF interval elapsed, switching variation for state: %s",
                         _current_state_name.c_str());
                _last_randomize_ms = now;
                playState(_current_state_name);
            }
        }
        return;
    }

    if (_current_state_name == "idle") {
        buildIdleGifs();
        if (_idle_emote_gifs.empty()) {
            return;
        }

        if (_idle_emote_playing) {
            if (_player->takeFinishedOnce()) {
                _last_randomize_ms = millis();
                playIdleBase();
            }
            return;
        }

        if (GIF_IDLE_INTERVAL_MS > 0) {
            uint32_t now = millis();
            bool loop_completed = _player->takeFinishedOnce();
            if ((uint32_t)(now - _last_randomize_ms) >= (uint32_t)GIF_IDLE_INTERVAL_MS &&
                loop_completed) {
                ESP_LOGD(TAG, "Idle GIF interval elapsed, playing emote");
                _last_randomize_ms = now;
                playIdleEmote();
            }
        }
        return;
    }

    // Check if the current GIF has finished one loop
    if (_player->hasFinishedOnce()) {
        // If we have variations for the current state, play a new random one
        if (_current_state_name.length() > 0 && hasVariations(_current_state_name)) {
            ESP_LOGD(TAG, "GIF finished loop, switching variation for state: %s",
                     _current_state_name.c_str());
            playState(_current_state_name); 
        }
    }
}

void GifManager::playState(const String& state_name, bool loop) {
    if (!_player) return;

    if (_emotion_active) {
        _resume_state_name = state_name;
        return;
    }

    bool state_changed = (_current_state_name != state_name);
    _current_state_name = state_name;

    if (_current_state_name == "idle") {
        buildIdleGifs();
        _last_randomize_ms = millis();
        playIdleBase();
        return;
    }

    if (state_changed ||
        _current_state_name == "listening" ||
        _current_state_name == "speaking") {
        _last_randomize_ms = millis();
    }

    String gif_path = getRandomStateGif(state_name);
    ESP_LOGI(TAG, "Manager playing state '%s' -> %s", state_name.c_str(), gif_path.c_str());
    _player->requestGIF(gif_path.c_str(), loop);
}

void GifManager::playEmotionOnce(const String& emotion_name) {
    if (!_player || emotion_name.length() == 0) {
        return;
    }

    if (!_state_discovered[emotion_name]) {
        discoverStateGifs(emotion_name);
    }

    std::vector<String> existing;
    auto it = _state_gifs_cache.find(emotion_name);
    if (it != _state_gifs_cache.end() && _fs) {
        for (const auto& path : it->second) {
            if (_fs->exists(path.c_str())) {
                existing.push_back(path);
            }
        }
    }

    if (existing.empty()) {
        ESP_LOGD(TAG, "No emotion GIF found for '%s', skipping", emotion_name.c_str());
        return;
    }

    String gif_path;
    if (existing.size() == 1) {
        gif_path = existing[0];
    } else {
        String selected_gif;
        int attempts = 0;
        do {
            uint32_t random_value = esp_random();
            int random_index = random_value % existing.size();
            selected_gif = existing[random_index];
            attempts++;
        } while (selected_gif == _last_played_path && attempts < 5);
        gif_path = selected_gif;
    }

    _last_played_path = gif_path;
    _last_emotion_state = emotion_name;
    _resume_state_name = _current_state_name;
    _emotion_active = true;
    _player->requestGIF(gif_path.c_str(), false);
}

void GifManager::playRandomEmotionOnce() {
    if (!_player) {
        return;
    }

    buildEmotionStates();
    if (_emotion_states.empty()) {
        ESP_LOGD(TAG, "No emotion states found, skipping random emotion");
        return;
    }

    String selected;
    int attempts = 0;
    do {
        uint32_t random_value = esp_random();
        int random_index = random_value % _emotion_states.size();
        selected = _emotion_states[random_index];
        attempts++;
    } while (selected == _last_emotion_state && attempts < 5);

    playEmotionOnce(selected);
}

void GifManager::discoverStateGifs(const String& state_name) {
  std::vector<String> gifs;
  
  if (!_fs) {
      ESP_LOGE(TAG, "LittleFSManager not initialized");
      return;
  }
  
  ESP_LOGD(TAG, "🔍 Discovering GIFs for state: %s in %s", state_name.c_str(), GIF_DIR);
  
  File dir = _fs->open(GIF_DIR);
  if (!dir || !dir.isDirectory()) {
    ESP_LOGW(TAG, "  ⚠️ Cannot open directory: %s", GIF_DIR);
    // Add fallback
    String fallback_path = String(GIF_DIR) + "/state_" + state_name + ".gif";
    gifs.push_back(fallback_path);
    _state_gifs_cache[state_name] = gifs;
    _state_discovered[state_name] = true;
    return;
  }
  
  // Pattern: state_<STATE>_<IDENTIFIER>.gif
  String pattern = "state_" + state_name + "_";
  
  // Scan for matching files
  File file = dir.openNextFile();
  while (file) {
    String full_path = file.name();
    // Some implementations return just name, others full path. Handle both.
    // We want just the filename part for pattern matching.
    String filename = full_path;
    int lastSlash = full_path.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = full_path.substring(lastSlash + 1);
    }
    
    // Check if filename matches pattern: state_<STATE>_<IDENTIFIER>.gif
    if (filename.startsWith(pattern) && filename.endsWith(".gif")) {
      // Ensure we store the full path for opening later
      // If file.name() was already full path, use it. If not, append to dir.
      String path_to_store = full_path;
      if (lastSlash < 0) {
          path_to_store = String(GIF_DIR) + "/" + filename;
      }
      
      gifs.push_back(path_to_store);
      ESP_LOGD(TAG, "  📁 Found: %s", filename.c_str());
    }
    
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
  
  // If no variations found, try default single GIF (state_<name>.gif)
  if (gifs.empty()) {
    String fallback_path = String(GIF_DIR) + "/state_" + state_name + ".gif";
    if (_fs->exists(fallback_path.c_str())) {
      gifs.push_back(fallback_path);
      ESP_LOGD(TAG, "  📁 Found default: state_%s.gif", state_name.c_str());
    } else {
      ESP_LOGW(TAG, "  ⚠️ No GIF found for state: %s", state_name.c_str());
      // Still add fallback to cache to avoid repeated lookups
      gifs.push_back(fallback_path);
    }
  } else {
    ESP_LOGD(TAG, "  ✅ Found %d variation(s) for %s state", gifs.size(), state_name.c_str());
  }
  
  // Cache the results
  _state_gifs_cache[state_name] = gifs;
  _state_discovered[state_name] = true;
}

String GifManager::getRandomStateGif(const String& state_name) {
  // Discover GIFs on first call for this state
  if (!_state_discovered[state_name]) {
    discoverStateGifs(state_name);
  }
  
  // Get the cached list for this state
  const std::vector<String>& gifs = _state_gifs_cache[state_name];
  
  // Check if we have valid GIFs
  bool has_valid_gif = false;
  if (!gifs.empty() && _fs) {
      // Check if at least the first one exists
      if (_fs->exists(gifs[0].c_str())) {
          has_valid_gif = true;
      }
  }

  // Fallback logic
  if (!has_valid_gif) {
    if (state_name != "idle") {
        ESP_LOGW(TAG, "⚠️ No GIF found for state: %s, falling back to 'idle'", state_name.c_str());
        return getRandomStateGif("idle");
    }
    
    // Last resort hard-coded path
    ESP_LOGW(TAG, "⚠️ No GIFs found for state: %s", state_name.c_str());
    return String(GIF_DIR) + "/state_idle.gif";
  }
  
  // Return random GIF from the list
  if (gifs.size() == 1) {
    _last_played_path = gifs[0];
    return gifs[0];
  }
  
  // Use ESP32 hardware RNG for better randomness
  // Try to pick a different GIF than the last one played
  String selected_gif;
  int attempts = 0;
  do {
      uint32_t random_value = esp_random();
      int random_index = random_value % gifs.size();
      selected_gif = gifs[random_index];
      attempts++;
  } while (selected_gif == _last_played_path && attempts < 5);
  
  _last_played_path = selected_gif;
  return selected_gif;
}

void GifManager::buildEmotionStates() {
  if (_emotion_states_built) {
      return;
  }

  _emotion_states_built = true;
  _emotion_states.clear();

  if (!_fs) {
      ESP_LOGE(TAG, "LittleFSManager not initialized (emotion states)");
      return;
  }

  File dir = _fs->open(GIF_DIR);
  if (!dir || !dir.isDirectory()) {
      ESP_LOGW(TAG, "Cannot open GIF dir for emotions: %s", GIF_DIR);
      return;
  }

  const char* excluded[] = {
      "angry",
      "dizzy",
      "excite",
      "idle",
      "listening",
      "speaking",
      "startup",
      "connecting",
      "sleep",
      "tap",
      "lowbat",
      "medbat",
      "highbat",
      "thinking"
  };
  const size_t excluded_count = sizeof(excluded) / sizeof(excluded[0]);

  std::map<String, bool> seen;
  File file = dir.openNextFile();
  while (file) {
      String full_path = file.name();
      String filename = full_path;
      int lastSlash = full_path.lastIndexOf('/');
      if (lastSlash >= 0) {
          filename = full_path.substring(lastSlash + 1);
      }

      if (filename.startsWith("state_") && filename.endsWith(".gif")) {
          String base = filename.substring(6);  // after "state_"
          int dot = base.lastIndexOf('.');
          if (dot > 0) {
              base = base.substring(0, dot);
          }
          int underscore = base.indexOf('_');
          if (underscore > 0) {
              base = base.substring(0, underscore);
          }

          bool is_excluded = false;
          for (size_t i = 0; i < excluded_count; ++i) {
              if (base == excluded[i]) {
                  is_excluded = true;
                  break;
              }
          }

          if (!is_excluded && !seen[base]) {
              seen[base] = true;
              _emotion_states.push_back(base);
          }
      }

      file.close();
      file = dir.openNextFile();
  }
  dir.close();

  if (_emotion_states.empty()) {
      ESP_LOGD(TAG, "No emotion states discovered in %s", GIF_DIR);
  } else {
      ESP_LOGD(TAG, "Discovered %d emotion states", _emotion_states.size());
  }
}

void GifManager::refreshState(const String& state_name) {
  ESP_LOGD(TAG, "🔄 Refreshing state: %s", state_name.c_str());
  _state_discovered[state_name] = false;
  _state_gifs_cache.erase(state_name);

  if (state_name == "idle") {
      _idle_gifs_built = false;
      _idle_emote_gifs.clear();
      _idle_base_gif = "";
      _last_idle_emote_path = "";
  }
}

void GifManager::refreshAll() {
  ESP_LOGD(TAG, "🔄 Refreshing all states");
  _state_discovered.clear();
  _state_gifs_cache.clear();

  _idle_gifs_built = false;
  _idle_emote_gifs.clear();
  _idle_base_gif = "";
  _last_idle_emote_path = "";

  _emotion_states_built = false;
  _emotion_states.clear();
  _last_emotion_state = "";
}

int GifManager::getVariationCount(const String& state_name) const {
  auto it = _state_gifs_cache.find(state_name);
  if (it != _state_gifs_cache.end()) {
    return it->second.size();
  }
  return 0;
}

bool GifManager::hasVariations(const String& state_name) const {
  return getVariationCount(state_name) > 1;
}
