/**
 * @file speaker_common.h
 * @brief Shared constants, types, and utilities for speaker system modules
 */

#ifndef SPEAKER_COMMON_H
#define SPEAKER_COMMON_H

#include "common.h"
#include <Arduino.h>

//==============================================================================
// CONSTANTS & DEFINITIONS
//==============================================================================

// IMPORTANT: I2S_WS_IO is assigned to A1 for SERIES_2 REVISION 2 that supports AXP2101.
// FOR SERIES_2 REVISION 1, I2S_WS_IO is just a dummy pin and not used. Because of this REVISION 1 does not support other audio formats such as wave or mp3.
// This was a hardware constraint with the LRCLK pin being tied to GND. REVISION 2 wires it to A1 which was previously used for ADXL345 INT pin.

// IMPORTANT: For SERIES 2 REVISION 1, MP3 and WAV audio is not supported because of the hardware constraint mentioned above.

// I2S Configuration is for code generated sounds only for now.
#define I2S_NUM I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_24BIT
#define I2S_CHANNEL_FORMAT I2S_CHANNEL_FMT_RIGHT_LEFT // SD_MODE pulled up to VDD via 634k resistor

// I2S Pin Configuration (shared between speaker_module and speaker_mp3)
#define I2S_BCK_IO A3
#define I2S_WS_IO A1
#define I2S_DO_IO A2

//==============================================================================
// TYPE DEFINITIONS
//==============================================================================

/**
 * @brief Audio operation modes (from speaker_module)
 */
typedef enum {
    AUDIO_MODE_IDLE,
    AUDIO_MODE_BEEP,
    AUDIO_MODE_SHUTDOWN
} audio_mode_t;

/**
 * @brief Audio system states (from speaker_module)
 */
typedef enum {
    AUDIO_STATE_DISABLED,
    AUDIO_STATE_NOT_READY,
    AUDIO_STATE_READY,
    AUDIO_STATE_PLAYING
} audio_state_t;

//==============================================================================
// SHARED STATE VARIABLES 
//==============================================================================

extern bool g_i2sInitializedForBeep;  // Track I2S state for beep mode
extern audio_mode_t g_audioMode;  // Current audio mode


#endif /* SPEAKER_COMMON_H */
