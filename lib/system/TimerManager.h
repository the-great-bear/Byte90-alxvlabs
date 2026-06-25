/**
 * TimerManager.h
 *
 * Declarations for TimerManager.
 */

#pragma once

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>

class NVSStorage;

static constexpr uint8_t TIMER_MAX_ENTRIES  = 8;
static constexpr uint8_t TIMER_LABEL_MAX    = 24;

/**
 * TimerManager - Multi-timer countdown service (up to 8 concurrent timers).
 *
 * Features:
 * - Up to TIMER_MAX_ENTRIES simultaneous named timers
 * - Each timer has a unique id (1-based), optional label, and DisplayFormat
 * - Timers persist across reboot via NVS (rehydrated if not yet expired)
 * - cancel()/repeat()/status queries accept an id; 0 targets the most-recent
 * - Backward-compatible single-timer query methods (isRunning, remainingSeconds,
 *   durationSeconds, displayFormat) reflect the soonest-expiring running timer
 *
 * Lifecycle / threading:
 * - Each timer owns a heap-allocated callback arg (stable address) and an
 *   esp_timer handle; both are freed when the timer is canceled or its expiry
 *   is consumed by update(), so neither _entries nor the esp_timer pool grows
 *   unbounded.
 * - Shared state is guarded by a FreeRTOS mutex (safe to hold across heap
 *   allocation and from the esp_timer dispatch task). esp_timer_delete is
 *   always called AFTER releasing the mutex to avoid deadlocking against the
 *   dispatch task.
 */
class TimerManager {
public:
    enum class DisplayFormat : uint8_t {
        None,
        Seconds,
        Minutes,
        Hours
    };

    struct TimerEntry {
        uint8_t id;
        char label[TIMER_LABEL_MAX + 1];
        uint32_t duration_seconds;
        DisplayFormat format;
        bool running;
        bool expired;
        uint64_t end_ms;
        long long end_epoch_ms;
        esp_timer_handle_t esp_timer;
        void* cb_arg;  // owning CallbackArg*, opaque to callers
    };

    using ExpiredCallback = std::function<void(uint8_t id, const char* label)>;

    TimerManager();
    ~TimerManager();

    // Start a new timer. Returns assigned id (1-8) on success, 0 on failure.
    uint8_t start(uint32_t duration_seconds);
    uint8_t start(uint32_t duration_seconds, DisplayFormat format);
    uint8_t start(uint32_t duration_seconds, const char* label, DisplayFormat format);

    // Cancel timer by id. id=0 targets the most-recently started.
    bool cancel(uint8_t id = 0);

    // Restart a previous timer by id. id=0 targets the most-recently started.
    uint8_t repeatTimer(uint8_t id = 0);

    // Legacy alias kept for call-site compatibility.
    bool repeat() { return repeatTimer(0) != 0; }

    // Status queries
    bool isRunning() const;
    bool isRunning(uint8_t id) const;
    std::vector<TimerEntry> listActive() const;
    const TimerEntry* getEntry(uint8_t id) const;
    uint8_t lastTimerId() const;

    // Single-timer compat: reflect the soonest-expiring running timer.
    uint32_t remainingSeconds() const;
    uint32_t durationSeconds() const;
    DisplayFormat displayFormat() const;

    // Most-recent-timer compat.
    uint32_t lastDurationSeconds() const;
    DisplayFormat lastDisplayFormat() const;

    void update();
    void setExpiredCallback(ExpiredCallback callback);

    // NVS persistence. Call persistTimers() on every mutation.
    void persistTimers(NVSStorage* storage);
    void rehydrateTimers(NVSStorage* storage);

private:
    struct CallbackArg {
        TimerManager* manager;
        uint8_t id;
    };

    static void onTimerExpired(void* arg);
    void markExpired(uint8_t id);

    // Caller must hold _mutex for the find* helpers.
    TimerEntry* findEntry(uint8_t id);
    const TimerEntry* findEntry(uint8_t id) const;
    const TimerEntry* soonestRunning() const;
    uint8_t allocId() const;

    // Destroy a detached timer's OS/heap resources. Call WITHOUT _mutex held.
    static void destroyResources(esp_timer_handle_t handle, void* cb_arg, bool stop_first);

    std::vector<TimerEntry> _entries;

    ExpiredCallback _expired_callback;
    mutable SemaphoreHandle_t _mutex;

    uint8_t _last_id;
    uint32_t _last_duration_seconds;
    DisplayFormat _last_format;
    char _last_label[TIMER_LABEL_MAX + 1];
};
