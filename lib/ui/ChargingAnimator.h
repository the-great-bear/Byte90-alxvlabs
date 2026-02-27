/**
 * ChargingAnimator.h
 *
 * Charging GIF playback controller.
 */

#pragma once

#include <Arduino.h>

#include "SystemState.h"

class AXP2101;
class GifManager;
class GifPlayer;
class Mp3Player;

class ChargingAnimator {
public:
    ChargingAnimator();

    void setGifManager(GifManager* manager);
    void setMp3Player(Mp3Player* player);

    bool isActive() const { return _active; }

    bool update(SystemState current_state, AXP2101* power_manager);

private:
    enum BatteryTier {
        BATTERY_TIER_LOW = 0,
        BATTERY_TIER_MED,
        BATTERY_TIER_HIGH
    };

    void startAnimation(const char* gif_path);
    void stopAndResumeIdle();
    const char* selectChargingGif(uint8_t battery_percentage, BatteryTier& tier_out) const;

    GifManager* _gif_manager;
    GifPlayer* _player;
    Mp3Player* _mp3_player;
    bool _active;
    int _loops_completed;
    const char* _current_gif;
    bool _was_charging;
    BatteryTier _last_tier;
    bool _sound_played;
};
