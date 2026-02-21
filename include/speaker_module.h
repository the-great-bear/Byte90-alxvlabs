/**
 * @file speaker_module.h
 * @brief Enhanced audio functionality with ESPHome ESP32-audioI2S support for MAX98357A codec
 */

#ifndef SPEAKER_MODULE_H
#define SPEAKER_MODULE_H

#include "common.h"
#include <Arduino.h>
#include <Preferences.h>
#include "flash_module.h"
#include "speaker_common.h"

//==============================================================================
// CONSTANTS & DEFINITIONS
//==============================================================================

static const char *SPEAKER_LOG = "::SPEAKER_MODULE::";

//==============================================================================
// PUBLIC API FUNCTIONS
//==============================================================================

/**
 * @brief Initialize the speaker module with optional preference checking
 * @param checkPreferences If true, checks user preferences before initialization
 * @return true if initialization successful, false otherwise
 */
bool initializeSpeaker(bool checkPreferences = true);

/**
 * @brief Shutdown audio system with optional preference saving
 * @param saveAsDisabled If true, saves the disabled state to persistent storage
 */
void shutdownAudio(bool saveAsDisabled = false);

/**
 * @brief Get comprehensive audio system state
 * @return audio_state_t indicating current state
 */
audio_state_t getAudioState(void);

/**
 * @brief Generate and play a single beep - Core audio function
 * @param frequency Frequency in Hz
 * @param duration Duration in milliseconds
 * @param volume Volume level (0-100)
 */
void playBeep(uint16_t frequency, uint16_t duration, uint8_t volume);

/**
 * @brief Enable or disable debug logging for speaker operations
 * @param enabled true to enable debug logging, false to disable
 */
void setSpeakerDebug(bool enabled);

#endif /* SPEAKER_MODULE_H */