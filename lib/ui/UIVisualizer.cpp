/**
 * UIVisualizer.cpp
 *
 * Implementation for UIVisualizer.
 */

#include "UIVisualizer.h"
#include "AudioVisualizer.h"
#include <math.h>

UIVisualizer::UIVisualizer(ArduinoSSD1351* display)
    : _display(display)
    , _audio_visualizer(nullptr)
    , _boost_level(4.0f)
    , _min_height(8)
    , _max_height(38)
    , _speaking_color(COLOR_CYAN)
    , _center_dot_index(0)
    , _activity_dot_index(0)
{
}

UIVisualizer::~UIVisualizer()
{
}

void UIVisualizer::setAudioVisualizer(AudioVisualizer* visualizer)
{
    _audio_visualizer = visualizer;
}

void UIVisualizer::setBoostLevel(float boost)
{
    _boost_level = boost;
}

void UIVisualizer::setBarHeightRange(int min_height, int max_height)
{
    _min_height = min_height;
    _max_height = max_height;
}

void UIVisualizer::setSpeakingColor(uint16_t color)
{
    _speaking_color = color;
}

void UIVisualizer::updateAnimation(SystemState current_state, bool ws_hello_received)
{
    bool should_update = (current_state == SYSTEM_STATE_LISTENING ||
                          current_state == SYSTEM_STATE_CONNECTING ||
                          current_state == SYSTEM_STATE_LOADING ||
                          (current_state == SYSTEM_STATE_IDLE && !ws_hello_received));

    if (should_update) {
        _center_dot_index = (_center_dot_index + 1) % 5;
        _activity_dot_index = (_activity_dot_index + 1) % 4;
    }
}

void UIVisualizer::createStatusVisualizer(SystemState current_state, bool ws_hello_received, bool ws_connecting)
{
    if (!_display) return;

    uint16_t dot_color = COLOR_WHITE;
    bool animate_opacity = false;
    bool animate_fft = false;
    bool show_static = false;
    uint8_t static_opacity = 255;

    if (current_state == SYSTEM_STATE_STARTING) {
        dot_color = COLOR_WHITE;
        show_static = true;
        static_opacity = 26;
    } else if (current_state == SYSTEM_STATE_IDLE) {
        if (ws_hello_received) {
            dot_color = COLOR_CYAN;
            show_static = true;
            static_opacity = 26;
        } else if (ws_connecting) {
            dot_color = COLOR_YELLOW;
            animate_opacity = true;
        } else {
            dot_color = COLOR_WHITE;
            show_static = true;
            static_opacity = 26;
        }
    } else if (current_state == SYSTEM_STATE_CONNECTING) {
        dot_color = COLOR_YELLOW;
        animate_opacity = true;
    } else if (current_state == SYSTEM_STATE_LOADING) {
        dot_color = COLOR_YELLOW;
        animate_opacity = true;
    } else if (current_state == SYSTEM_STATE_LISTENING) {
        dot_color = COLOR_CYAN;
        animate_opacity = true;
    } else if (current_state == SYSTEM_STATE_SPEAKING) {
        dot_color = COLOR_CYAN;
        animate_fft = true;
    } else {
        return;
    }

    float level = 0.0f;
    if (animate_fft && _audio_visualizer) {
        // Use RMS level instead of smoothed level for more "bounce"
        level = _audio_visualizer->getRmsLevel();
    }

    for (int i = 0; i < 4; i++) {
        int x = _ACTIVITY_DOTS_X + (i * _ACTIVITY_DOT_SPACING);
        int height = 4;
        uint8_t opacity = 255;

        if (show_static) {
            opacity = static_opacity;
        } else if (animate_opacity) {
            int offset = (i - _activity_dot_index + 4) % 4;
            switch (offset) {
                case 0: opacity = 255; break;
                case 1: opacity = 153; break;  // 60%
                case 2: opacity = 102; break;  // 40%
                case 3: opacity = 77; break;   // 30%
                default: opacity = 77; break;
            }
        } else if (animate_fft && level > 0.005f) {
            // Apply high gain (12.0x) and non-linear scaling (sqrt) to boost visibility
            float boosted_level = sqrtf(level * 12.0f);
            
            // Simulate random variation based on boosted level
            float variation = 1.0f - ((i % 2) * 0.2f);
            float dot_level = boosted_level * variation;
            
            dot_level = fmaxf(0.0f, fminf(1.0f, dot_level));
            height = 1 + (int)(dot_level * 5);
            if (height < 1) height = 1;
            if (height > 6) height = 6;
            // Full 100% opacity during speaking
            opacity = 255;
        } else if (animate_fft) {
            height = 2;
            opacity = 255;
        }

        int y = _ACTIVITY_DOTS_Y - (height / 2);
        
        int max_y = _ACTIVITY_DOTS_Y - 3;
        int max_height = 6;
        
        // Clear above
        if (y > max_y) {
            _display->getAdafruitDisplay()->fillRect(x, max_y, _ACTIVITY_DOT_WIDTH, y - max_y, COLOR_BLACK);
        }
        
        // Clear below
        int bar_bottom = y + height;
        int max_bottom = max_y + max_height;
        if (bar_bottom < max_bottom) {
            _display->getAdafruitDisplay()->fillRect(x, bar_bottom, _ACTIVITY_DOT_WIDTH, max_bottom - bar_bottom, COLOR_BLACK);
        }

        uint16_t final_color = dot_color;
        if (opacity < 255) {
            int r = ((dot_color >> 11) & 0x1F) * opacity / 255;
            int g = ((dot_color >> 5) & 0x3F) * opacity / 255;
            int b = (dot_color & 0x1F) * opacity / 255;
            final_color = (r << 11) | (g << 5) | b;
        }

        _display->getAdafruitDisplay()->fillRect(x, y, _ACTIVITY_DOT_WIDTH, height, final_color);
    }
}

void UIVisualizer::createVisualizer(SystemState current_state, bool ws_hello_received, bool ws_connecting)
{
    if (!_display) return;

    uint16_t bar_color = COLOR_WHITE;
    bool animate_opacity = false;
    bool animate_fft = false;
    bool show_faint = false;

    if (current_state == SYSTEM_STATE_STARTING) {
        return;
    } else if (current_state == SYSTEM_STATE_IDLE) {
        if (ws_hello_received) {
            bar_color = COLOR_CYAN; // Keep consistent with original or use _speaking_color? Original used CYAN
            show_faint = true;
        } else if (ws_connecting) {
            bar_color = COLOR_YELLOW;
            animate_opacity = true;
        } else {
            bar_color = COLOR_WHITE;
            show_faint = true;
        }
    } else if (current_state == SYSTEM_STATE_CONNECTING) {
        bar_color = COLOR_YELLOW;
        animate_opacity = true;
    } else if (current_state == SYSTEM_STATE_LISTENING) {
        bar_color = COLOR_CYAN;
        animate_opacity = true;
    } else if (current_state == SYSTEM_STATE_SPEAKING) {
        bar_color = _speaking_color;
        animate_fft = true;
    } else {
        return;
    }

    float level = 0.0f;
    if (animate_fft && _audio_visualizer) {
        // Use RMS level for more immediate bounce
        level = _audio_visualizer->getRmsLevel();
    }

    const int total_width = (5 * BAR_WIDTH) + (4 * BAR_GAP);
    const int start_x = (_SCREEN_WIDTH - total_width) / 2;

    for (int i = 0; i < 5; i++) {
        int x = start_x + (i * (BAR_WIDTH + BAR_GAP));
        int height = _min_height; // Default to min height
        uint8_t opacity = 255;

        if (show_faint) {
            opacity = 26;
        } else if (animate_opacity) {
            int offset = (i - _center_dot_index + 5) % 5;
            switch (offset) {
                case 0: opacity = 255; break;
                case 1: opacity = 153; break;  // 60%
                case 2: opacity = 102; break;  // 40%
                case 3: opacity = 77; break;   // 30%
                default: opacity = 77; break;
            }
        } else if (animate_fft && level > 0.005f) {
            // Apply high gain and non-linear scaling (sqrt) to boost visibility
            float boosted_level = sqrtf(level * _boost_level);

            // Simulate frequency bands distribution with increased variation
            float variation = 1.0f;
            if (i == 0 || i == 4) variation = 0.7f;
            else if (i == 1 || i == 3) variation = 0.9f;
            
            float bar_level = boosted_level * variation;
            bar_level = fmaxf(0.0f, fminf(1.0f, bar_level));
            
            height = _min_height + (int)(bar_level * (_max_height - _min_height));
            if (height < _min_height) height = _min_height;
            if (height > _max_height) height = _max_height;
            // Full 100% opacity during speaking
            opacity = 255;
        } else if (animate_fft) {
            height = _min_height;
            opacity = 255;
        }

        int y = (_SCREEN_HEIGHT - height) / 2;
        int max_y = (_SCREEN_HEIGHT - _max_height) / 2;
        
        // Clear above
        if (y > max_y) {
            _display->getAdafruitDisplay()->fillRect(x, max_y, BAR_WIDTH, y - max_y, COLOR_BLACK);
        }
        
        // Clear below
        int bar_bottom = y + height;
        int max_bottom = max_y + _max_height;
        if (bar_bottom < max_bottom) {
            _display->getAdafruitDisplay()->fillRect(x, bar_bottom, BAR_WIDTH, max_bottom - bar_bottom, COLOR_BLACK);
        }

        uint16_t final_color = bar_color;
        if (opacity < 255) {
            int r = ((bar_color >> 11) & 0x1F) * opacity / 255;
            int g = ((bar_color >> 5) & 0x3F) * opacity / 255;
            int b = (bar_color & 0x1F) * opacity / 255;
            final_color = (r << 11) | (g << 5) | b;
        }

        _display->getAdafruitDisplay()->fillRect(x, y, BAR_WIDTH, height, final_color);
    }
}
