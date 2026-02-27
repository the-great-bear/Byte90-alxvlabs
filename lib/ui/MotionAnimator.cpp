/**
 * MotionAnimator.cpp
 *
 * Implementation for MotionAnimator.
 */

#include "MotionAnimator.h"
#include "AdxlManager.h"
#include "GifManager.h"
#include "GifPlayer.h"
#include "HapticsManager.h"
#include "Mp3Player.h"
#include "SystemState.h"
#include <LittleFS.h>

namespace {
constexpr const char* SHAKE_GIF = "/gifs/state_dizzy.gif";
constexpr const char* TAP_GIF = "/gifs/state_tap.gif";
constexpr const char* DOUBLE_TAP_GIF = "/gifs/state_idle_1.gif";
constexpr const char* LIFT_GIF = "/gifs/state_excite.gif";
constexpr const char* SHAKE_SOUND = "/sounds/confused.mp3";
constexpr const char* TAP_SOUND = "/sounds/tap.mp3";
constexpr const char* DOUBLE_TAP_SOUND = "/sounds/crash.mp3";
constexpr unsigned long SHAKE_DEBOUNCE_MS = 800;
} // namespace

MotionAnimator::MotionAnimator()
    : _adxl_manager(nullptr)
    , _gif_manager(nullptr)
    , _player(nullptr)
    , _mp3_player(nullptr)
    , _haptics_manager(nullptr)
    , _active(false)
    , _resume_requested(false)
    , _shake_seen(false)
    , _tap_seen(false)
    , _double_tap_seen(false)
    , _lift_seen(false)
    , _last_shake_ms(0)
{
}

void MotionAnimator::setAdxlManager(AdxlManager* adxl_manager) {
    _adxl_manager = adxl_manager;
}

void MotionAnimator::setGifManager(GifManager* gif_manager) {
    _gif_manager = gif_manager;
    _player = gif_manager ? gif_manager->getPlayer() : nullptr;
}

void MotionAnimator::setMp3Player(Mp3Player* mp3_player) {
    _mp3_player = mp3_player;
}

void MotionAnimator::setHapticsManager(HapticsManager* haptics_manager) {
    _haptics_manager = haptics_manager;
}

bool MotionAnimator::isActive() const {
    return _active;
}

bool MotionAnimator::needsResume() const {
    return _resume_requested;
}

void MotionAnimator::consumeResumeRequest() {
    _resume_requested = false;
}

void MotionAnimator::playFeedback(const char* sound_path, bool play_haptic) {
    if (_mp3_player && sound_path) {
        _mp3_player->playFile(sound_path);
    }
    if (play_haptic &&
        _haptics_manager &&
        _haptics_manager->isReady() &&
        _haptics_manager->isEnabled()) {
        _haptics_manager->playEventHaptic(HapticsManager::HAPTIC_EVENT_INTERRUPT);
    }
}

void MotionAnimator::triggerGif(const char* path, const char* sound_path, bool play_haptic) {
    if (!_player || !path) {
        return;
    }
    _active = true;
    _resume_requested = false;
    ESP_LOGI("MotionAnimator", "Trigger GIF: %s (active=%d finished=%d)",
             path,
             _active ? 1 : 0,
             _player->hasFinishedOnce() ? 1 : 0);
    if (LittleFS.exists(path)) {
        _player->requestGIF(path, false);
    } else if (_gif_manager) {
        _gif_manager->playState("idle");
    }
    playFeedback(sound_path, play_haptic);
}

bool MotionAnimator::update(SystemState resume_state, bool allow_trigger) {
    (void)resume_state;

    if (!_player || !_adxl_manager) {
        _active = false;
        _resume_requested = false;
        return false;
    }

    if (_active && _player->hasFinishedOnce()) {
        _active = false;
        _resume_requested = true;
    }

    if (!allow_trigger || _active) {
        return _active;
    }

    bool shake = _adxl_manager->checkMotionState(MotionState::SHAKING);
    bool tap = _adxl_manager->checkMotionState(MotionState::TAPPED);
    bool double_tap = _adxl_manager->checkMotionState(MotionState::DOUBLE_TAPPED);
    bool lifted = _adxl_manager->checkMotionState(MotionState::LIFTED);

    unsigned long now = millis();

    if (shake && !_shake_seen) {
        _shake_seen = true;
        if (now - _last_shake_ms >= SHAKE_DEBOUNCE_MS) {
            _last_shake_ms = now;
            triggerGif(SHAKE_GIF, SHAKE_SOUND, true);
            return _active;
        }
    } else if (!shake) {
        _shake_seen = false;
    }

    if (lifted && !_lift_seen) {
        _lift_seen = true;
        triggerGif(LIFT_GIF, nullptr, false);
        return _active;
    } else if (!lifted) {
        _lift_seen = false;
    }

    if (double_tap && !_double_tap_seen) {
        _double_tap_seen = true;
        triggerGif(DOUBLE_TAP_GIF, DOUBLE_TAP_SOUND, true);
        return _active;
    } else if (!double_tap) {
        _double_tap_seen = false;
    }

    if (tap && !_tap_seen) {
        _tap_seen = true;
        triggerGif(TAP_GIF, TAP_SOUND, true);
        return _active;
    } else if (!tap) {
        _tap_seen = false;
    }

    return _active;
}
