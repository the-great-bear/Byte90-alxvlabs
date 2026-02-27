/**
 * ChargingAnimator.cpp
 *
 * Charging GIF playback controller.
 */

#include "ChargingAnimator.h"

#include "Axp2101.h"
#include "GifManager.h"
#include "GifPlayer.h"
#include "SystemState.h"
#include "Mp3Player.h"
#include <esp_log.h>

static const char* TAG = "ChargingAnimator";

static constexpr int MAX_LOOPS = 2;
static constexpr uint8_t LOW_BATTERY_THRESHOLD = 20;
static constexpr uint8_t MED_BATTERY_THRESHOLD = 60;

static constexpr const char* LOW_BATTERY_GIF = "/gifs/state_lowbat.gif";
static constexpr const char* MED_BATTERY_GIF = "/gifs/state_medbat.gif";
static constexpr const char* HIGH_BATTERY_GIF = "/gifs/state_highbat.gif";

ChargingAnimator::ChargingAnimator()
    : _gif_manager(nullptr)
    , _player(nullptr)
    , _mp3_player(nullptr)
    , _active(false)
    , _loops_completed(0)
    , _current_gif(nullptr)
    , _was_charging(false)
    , _last_tier(BATTERY_TIER_HIGH)
    , _sound_played(false)
{
}

void ChargingAnimator::setGifManager(GifManager* manager)
{
    _gif_manager = manager;
    _player = manager ? manager->getPlayer() : nullptr;
}

void ChargingAnimator::setMp3Player(Mp3Player* player)
{
    _mp3_player = player;
}

bool ChargingAnimator::update(SystemState current_state, AXP2101* power_manager)
{
    if (!_gif_manager || !_player || !power_manager) {
        if (_active) {
            stopAndResumeIdle();
        }
        _was_charging = false;
        _sound_played = false;
        return false;
    }

    if (current_state != SYSTEM_STATE_IDLE) {
        if (_active) {
            stopAndResumeIdle();
        }
        _was_charging = false;
        _sound_played = false;
        return false;
    }

    bool charging = power_manager->isVbusIn() && power_manager->isCharging();
    if (!charging) {
        if (_active) {
            stopAndResumeIdle();
        }
        _was_charging = false;
        _sound_played = false;
        return false;
    }

    uint8_t battery_percentage = 0;
    bool battery_connected = power_manager->getBatteryPercentage(&battery_percentage);
    if (!battery_connected) {
        if (_active) {
            stopAndResumeIdle();
        }
        _was_charging = false;
        _sound_played = false;
        return false;
    }

    if (!_sound_played && _mp3_player) {
        _mp3_player->playFile("/sounds/charging.mp3");
        _sound_played = true;
    }

    BatteryTier desired_tier = _last_tier;
    const char* desired_gif = selectChargingGif(battery_percentage, desired_tier);
    bool tier_changed = desired_tier != _last_tier;
    if (!_was_charging || tier_changed) {
        startAnimation(desired_gif);
        _last_tier = desired_tier;
    }

    if (_player->takeFinishedOnce()) {
        _loops_completed++;
        if (_loops_completed >= MAX_LOOPS) {
            stopAndResumeIdle();
        }
    }

    _was_charging = true;
    return _active;
}

void ChargingAnimator::startAnimation(const char* gif_path)
{
    _current_gif = gif_path;
    _loops_completed = 0;
    _active = true;
    ESP_LOGI(TAG, "Charging animation start: %s", gif_path);
    _player->requestGIF(gif_path, true);
}

void ChargingAnimator::stopAndResumeIdle()
{
    if (_player) {
        _player->stopPlayback();
    }
    if (_gif_manager) {
        _gif_manager->playState("idle");
    }
    _active = false;
    _current_gif = nullptr;
    _loops_completed = 0;
}

const char* ChargingAnimator::selectChargingGif(uint8_t battery_percentage, BatteryTier& tier_out) const
{
    if (battery_percentage <= LOW_BATTERY_THRESHOLD) {
        tier_out = BATTERY_TIER_LOW;
        return LOW_BATTERY_GIF;
    }
    if (battery_percentage <= MED_BATTERY_THRESHOLD) {
        tier_out = BATTERY_TIER_MED;
        return MED_BATTERY_GIF;
    }
    tier_out = BATTERY_TIER_HIGH;
    return HIGH_BATTERY_GIF;
}
