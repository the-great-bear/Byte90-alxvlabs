/**
 * TimerManager.h
 *
 * Declarations for TimerManager.
 */

#pragma once

#include <Arduino.h>
#include <esp_timer.h>
#include <functional>

/**
 * TimerManager - Single countdown timer service.
 *
 * Features:
 * - One-shot timer with expiry callback
 * - Single active timer at a time
 * - Last duration retained in RAM for repeat
 */
class TimerManager {
public:
    using ExpiredCallback = std::function<void()>;
    enum class DisplayFormat : uint8_t {
        None,
        Seconds,
        Minutes,
        Hours
    };

    TimerManager();
    ~TimerManager();

    bool start(uint32_t duration_seconds);
    bool start(uint32_t duration_seconds, DisplayFormat format);
    bool cancel();
    bool repeat();

    bool isRunning() const;
    uint32_t durationSeconds() const;
    uint32_t remainingSeconds() const;
    uint32_t lastDurationSeconds() const;
    DisplayFormat displayFormat() const;
    DisplayFormat lastDisplayFormat() const;

    void update();
    void setExpiredCallback(ExpiredCallback callback);

private:
    static void onTimerExpired(void* arg);
    void markExpired();

    esp_timer_handle_t _timer;
    ExpiredCallback _expired_callback;

    mutable portMUX_TYPE _timer_mux;
    bool _running;
    bool _expired;
    uint32_t _duration_seconds;
    uint32_t _last_duration_seconds;
    uint64_t _end_ms;
    DisplayFormat _display_format;
    DisplayFormat _last_display_format;
};
