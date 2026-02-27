/**
 * MotionAnimator.h
 *
 * Motion-triggered animation and feedback helpers.
 */

#pragma once

#include <Arduino.h>
#include "SystemState.h"

class AdxlManager;
class GifManager;
class GifPlayer;
class Mp3Player;
class HapticsManager;

class MotionAnimator {
public:
    MotionAnimator();

    void setAdxlManager(AdxlManager* adxl_manager);
    void setGifManager(GifManager* gif_manager);
    void setMp3Player(Mp3Player* mp3_player);
    void setHapticsManager(HapticsManager* haptics_manager);

    bool update(SystemState resume_state, bool allow_trigger);
    bool isActive() const;
    bool needsResume() const;
    void consumeResumeRequest();

private:
    void playFeedback(const char* sound_path, bool play_haptic);
    void triggerGif(const char* path, const char* sound_path, bool play_haptic);

    AdxlManager* _adxl_manager;
    GifManager* _gif_manager;
    GifPlayer* _player;
    Mp3Player* _mp3_player;
    HapticsManager* _haptics_manager;

    bool _active;
    bool _resume_requested;
    bool _shake_seen;
    bool _tap_seen;
    bool _double_tap_seen;
    bool _lift_seen;
    unsigned long _last_shake_ms;
};
