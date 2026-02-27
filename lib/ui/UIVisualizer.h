/**
 * UIVisualizer.h
 *
 * Declarations for UIVisualizer.
 */

#pragma once

#include "ArduinoSSD1351.h"
#include "SystemState.h"
#include "StatusBarLayout.h"
#include <Arduino.h>

// Forward declarations
/**
 * @brief AudioVisualizer.
 */
class AudioVisualizer;

/**
 * @brief UIVisualizer.
 */
class UIVisualizer {
public:
    UIVisualizer(ArduinoSSD1351* display);
    ~UIVisualizer();

    void setAudioVisualizer(AudioVisualizer* visualizer);
    
    // Configuration
    void setBoostLevel(float boost);
    void setBarHeightRange(int min_height, int max_height);
    void setSpeakingColor(uint16_t color);

    // Update animation state (increment internal indices)
    void updateAnimation(SystemState current_state, bool ws_hello_received);

    // Draw the center visualization
    void createVisualizer(SystemState current_state, bool ws_hello_received, bool ws_connecting);
    
    // Draw the activity dots (top left)
    void createStatusVisualizer(SystemState current_state, bool ws_hello_received, bool ws_connecting);

private:
    ArduinoSSD1351* _display;
    AudioVisualizer* _audio_visualizer;

    float _boost_level;
    int _min_height;
    int _max_height;
    uint16_t _speaking_color;

    uint8_t _center_dot_index;
    uint8_t _activity_dot_index;
    
    // Constants
    static const int _SCREEN_WIDTH = 128;
    static const int _SCREEN_HEIGHT = 128;
    
    // Center bar constants
    static const int BAR_WIDTH = 10;
    static const int BAR_GAP = 2;
    
    // Activity dot constants
    static const int _ACTIVITY_DOTS_X = ACTIVITY_DOTS_X;
    static const int _ACTIVITY_DOTS_Y = ACTIVITY_DOTS_Y;
    static const int _ACTIVITY_DOT_WIDTH = ACTIVITY_DOT_WIDTH;
    static const int _ACTIVITY_DOT_GAP = ACTIVITY_DOT_GAP;
    static const int _ACTIVITY_DOT_SPACING = ACTIVITY_DOT_SPACING;
};
