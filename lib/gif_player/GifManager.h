/**
 * @file GifManager.h
 * @brief Manages GIF file discovery and randomization for state animations
 * 
 * Provides dynamic discovery of GIF variations for different states (speaking, idle, listening)
 * and random selection from available variations. Caches discovered files for performance.
 * Also manages the GifPlayer instance to orchestrate playback.
 */

#pragma once

#include "../storage/LittlefsManager.h"
#include "GifPlayer.h"
#include <Arduino.h>
#include <map>
#include <vector>

/**
 * @brief ArduinoSSD1351.
 */
class ArduinoSSD1351;

/**
 * @brief GifManager.
 */
class GifManager {
public:
  GifManager();
  ~GifManager();

  /**
   * @brief Initialize the GIF manager and player
   * @param fs Pointer to the LittleFS manager
   * @param display Pointer to the display instance
   * @param display_mutex Mutex for display access protection
   * @return true if initialization successful
   */
  bool begin(LittleFSManager* fs, ArduinoSSD1351* display, SemaphoreHandle_t display_mutex);

  /**
   * @brief Get a random GIF path for a given state
   * @param state_name The state name (e.g., "speaking", "idle", "listening")
   * @return Full path to a random GIF for that state
   * 
   * On first call for a state, discovers all matching GIF files.
   * Subsequent calls return random selections from cached list.
   * Falls back to default single GIF if no variations found.
   */
  String getRandomStateGif(const String& state_name);

  /**
   * @brief Play a GIF for the specified state
   * @param state_name The state name to play
   * @param loop Whether to loop the animation (default true)
   */
  void playState(const String& state_name, bool loop = true);

  /**
   * @brief Play a one-shot emotion GIF then resume current state
   * @param emotion_name Emotion name (e.g., "happy")
   */
  void playEmotionOnce(const String& emotion_name);

  /**
   * @brief Play a random emotion GIF (one-shot) then resume current state
   */
  void playRandomEmotionOnce();

  /**
   * @brief Force re-discovery of GIFs for a specific state
   * @param state_name The state name to refresh
   * 
   * Useful if GIF files are added/removed at runtime
   */
  void refreshState(const String& state_name);

  /**
   * @brief Force re-discovery of all state GIFs
   */
  void refreshAll();

  /**
   * @brief Update the manager state (checks for animation completion to randomize)
   */
  void update();

  /**
   * @brief Get the number of variations available for a state
   * @param state_name The state name
   * @return Number of GIF variations (0 if not yet discovered)
   */
  int getVariationCount(const String& state_name) const;

  /**
   * @brief Check if a state has multiple variations
   * @param state_name The state name
   * @return true if state has more than one GIF variation
   */
  bool hasVariations(const String& state_name) const;

  /**
   * @brief Check if any GIFs are present in the filesystem
   * @param fs Pointer to the LittleFS manager
   * @return true if GIF directory exists and contains at least one .gif file
   */
  static bool hasGifs(LittleFSManager* fs);

  /**
   * @brief Get the underlying GifPlayer instance
   * @return Pointer to GifPlayer
   */
  GifPlayer* getPlayer() const { return _player; }

private:
  void buildIdleGifs();
  void playIdleBase();
  void playIdleEmote();
  void buildEmotionStates();

  /**
   * @brief Discover all GIF variations for a specific state
   * @param state_name The state name to discover
   */
  void discoverStateGifs(const String& state_name);

  // Cache for each state type (speaking, idle, listening, etc.)
  std::map<String, std::vector<String>> _state_gifs_cache;
  
  // Track which states have been discovered
  std::map<String, bool> _state_discovered;

  // Gif Player instance
  GifPlayer* _player;
  
  // Filesystem manager
  LittleFSManager* _fs;

  // Current active state name
  String _current_state_name;
  bool _emotion_active;
  String _resume_state_name;
  
  // Last played GIF path to avoid repetition
  String _last_played_path;

  // Last time we randomized a timed state (ms from millis())
  uint32_t _last_randomize_ms;

  bool _idle_gifs_built;
  bool _idle_emote_playing;
  String _idle_base_gif;
  String _last_idle_emote_path;
  std::vector<String> _idle_emote_gifs;

  std::vector<String> _emotion_states;
  bool _emotion_states_built;
  String _last_emotion_state;

  // Base directory for GIF files
  static constexpr const char* GIF_DIR = "/gifs";
};
