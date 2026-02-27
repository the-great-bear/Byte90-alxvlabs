/**
 * SleepAnimator.h
 *
 * Sleep animation sequencing for inactivity-triggered sleep mode.
 */

#pragma once

#include <Arduino.h>

class GifManager;
class GifPlayer;

class SleepAnimator {
public:
    SleepAnimator();

    void setGifManager(GifManager* manager);
    void update(bool sleeping);

    bool isActive() const;
    bool needsResume() const;
    void consumeResumeRequest();

private:
    enum class Phase : uint8_t {
        Idle,
        Entering,
        Looping,
        Exiting
    };

    bool shouldPlayOnSleep() const;
    void requestGif(const char* path, bool loop);

    GifManager* _gif_manager;
    GifPlayer* _player;
    Phase _phase;
    bool _sleeping;
    bool _selected;
    bool _resume_requested;
};
