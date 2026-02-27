/**
 * SleepAnimator.cpp
 *
 * Implementation for SleepAnimator.
 */

#include "SleepAnimator.h"
#include "GifManager.h"
#include "GifPlayer.h"
#include <esp_log.h>
#include <LittleFS.h>

namespace {
constexpr const char* SLEEP_ENTER_GIF = "/gifs/state_sleep_1.gif";
constexpr const char* SLEEP_LOOP_GIF = "/gifs/state_sleep_2.gif";
constexpr const char* SLEEP_EXIT_GIF = "/gifs/state_sleep_3.gif";
constexpr uint8_t SLEEP_PLAY_PERCENT = 50;
static const char* TAG = "SleepAnimator";
} // namespace

SleepAnimator::SleepAnimator()
    : _gif_manager(nullptr)
    , _player(nullptr)
    , _phase(Phase::Idle)
    , _sleeping(false)
    , _selected(false)
    , _resume_requested(false)
{
}

void SleepAnimator::setGifManager(GifManager* manager) {
    _gif_manager = manager;
    _player = manager ? manager->getPlayer() : nullptr;
}

bool SleepAnimator::isActive() const {
    return _phase != Phase::Idle;
}

bool SleepAnimator::needsResume() const {
    return _resume_requested;
}

void SleepAnimator::consumeResumeRequest() {
    _resume_requested = false;
}

bool SleepAnimator::shouldPlayOnSleep() const {
    uint32_t value = static_cast<uint32_t>(random(0, 100));
    return value < SLEEP_PLAY_PERCENT;
}

void SleepAnimator::requestGif(const char* path, bool loop) {
    if (!_player || !path) {
        return;
    }
    if (LittleFS.exists(path)) {
        _player->requestGIF(path, loop);
    } else if (_gif_manager) {
        _gif_manager->playState("idle");
    }
}

void SleepAnimator::update(bool sleeping) {
    bool was_sleeping = _sleeping;
    _sleeping = sleeping;

    if (!_player) {
        _selected = false;
        _phase = Phase::Idle;
        return;
    }

    if (sleeping && !was_sleeping) {
        _selected = shouldPlayOnSleep();
        if (_selected) {
            ESP_LOGI(TAG, "Sleep animation selected");
            _phase = Phase::Entering;
            _resume_requested = false;
            requestGif(SLEEP_ENTER_GIF, false);
        } else {
            ESP_LOGI(TAG, "Sleep animation skipped");
            _phase = Phase::Idle;
        }
    }

    if (!sleeping && was_sleeping) {
        if (_selected && (_phase == Phase::Entering || _phase == Phase::Looping)) {
            _phase = Phase::Exiting;
            requestGif(SLEEP_EXIT_GIF, false);
        } else {
            _selected = false;
            _phase = Phase::Idle;
        }
    }

    if (!_selected) {
        return;
    }

    if (_phase == Phase::Entering) {
        if (_player->hasFinishedOnce()) {
            _phase = Phase::Looping;
            requestGif(SLEEP_LOOP_GIF, true);
        }
        return;
    }

    if (_phase == Phase::Exiting) {
        if (_player->hasFinishedOnce()) {
            _phase = Phase::Idle;
            _selected = false;
            _resume_requested = true;
        }
    }
}
