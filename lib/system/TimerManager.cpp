/**
 * TimerManager.cpp
 *
 * Implementation for TimerManager.
 */

#include "TimerManager.h"
#include "NvsStorage.h"
#include "ClockSync.h"

#include <esp_log.h>
#include <algorithm>
#include <cstring>

static const char* TAG = "TimerManager";

#define NS_TIMERS   "timers"
#define KEY_COUNT   "count"

TimerManager::TimerManager()
    : _expired_callback(nullptr)
    , _mux(portMUX_INITIALIZER_UNLOCKED)
    , _last_id(0)
    , _last_duration_seconds(0)
    , _last_format(DisplayFormat::None) {
    _entries.reserve(TIMER_MAX_ENTRIES);
    _cb_args.reserve(TIMER_MAX_ENTRIES);
}

TimerManager::~TimerManager() {
    for (auto& e : _entries) {
        if (e.esp_timer) {
            esp_timer_stop(e.esp_timer);
            esp_timer_delete(e.esp_timer);
        }
    }
    _entries.clear();
    _cb_args.clear();
}

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------

uint8_t TimerManager::start(uint32_t duration_seconds) {
    return start(duration_seconds, "", DisplayFormat::Seconds);
}

uint8_t TimerManager::start(uint32_t duration_seconds, DisplayFormat format) {
    return start(duration_seconds, "", format);
}

uint8_t TimerManager::start(uint32_t duration_seconds, const char* label, DisplayFormat format) {
    if (duration_seconds == 0) {
        return 0;
    }

    portENTER_CRITICAL(&_mux);
    size_t active = 0;
    for (auto& e : _entries) {
        if (e.running) active++;
    }
    if (active >= TIMER_MAX_ENTRIES) {
        portEXIT_CRITICAL(&_mux);
        ESP_LOGW(TAG, "Max timers reached");
        return 0;
    }
    portEXIT_CRITICAL(&_mux);

    uint8_t id = allocId();
    if (id == 0) {
        return 0;
    }

    // Build the callback arg before pushing (pointer must be stable).
    // We use _cb_args as a parallel stable-storage vector.
    _cb_args.push_back({this, id});
    CallbackArg* arg_ptr = &_cb_args.back();

    esp_timer_handle_t handle = nullptr;
    esp_timer_create_args_t args = {};
    args.callback = &TimerManager::onTimerExpired;
    args.arg = arg_ptr;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "timer_mgr";
    if (esp_timer_create(&args, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed for id %u", id);
        _cb_args.pop_back();
        return 0;
    }

    if (esp_timer_start_once(handle, static_cast<uint64_t>(duration_seconds) * 1000000ULL) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_once failed for id %u", id);
        esp_timer_delete(handle);
        _cb_args.pop_back();
        return 0;
    }

    TimerEntry entry = {};
    entry.id = id;
    size_t copy_len = label ? strnlen(label, TIMER_LABEL_MAX) : 0;
    if (copy_len) {
        memcpy(entry.label, label, copy_len);
    }
    entry.label[copy_len] = '\0';
    entry.duration_seconds = duration_seconds;
    entry.format = format;
    entry.running = true;
    entry.expired = false;
    entry.end_ms = millis() + static_cast<uint64_t>(duration_seconds) * 1000ULL;
    ClockSync cs;
    long long now_epoch = cs.epochMs();
    entry.end_epoch_ms = now_epoch > 0
        ? now_epoch + static_cast<long long>(duration_seconds) * 1000LL
        : 0;
    entry.esp_timer = handle;

    portENTER_CRITICAL(&_mux);
    _entries.push_back(entry);
    _last_id = id;
    _last_duration_seconds = duration_seconds;
    _last_format = format;
    portEXIT_CRITICAL(&_mux);

    ESP_LOGI(TAG, "Timer %u started (%u s, label='%s')", id, duration_seconds, entry.label);
    return id;
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

bool TimerManager::cancel(uint8_t id) {
    portENTER_CRITICAL(&_mux);
    uint8_t target = (id == 0) ? _last_id : id;
    TimerEntry* e = findEntry(target);
    if (!e || !e->running) {
        portEXIT_CRITICAL(&_mux);
        return false;
    }
    e->running = false;
    e->expired = false;
    e->end_ms = 0;
    esp_timer_handle_t handle = e->esp_timer;
    portEXIT_CRITICAL(&_mux);

    if (handle) {
        esp_timer_stop(handle);
    }
    ESP_LOGI(TAG, "Timer %u canceled", target);
    return true;
}

// ---------------------------------------------------------------------------
// Repeat
// ---------------------------------------------------------------------------

uint8_t TimerManager::repeatTimer(uint8_t id) {
    portENTER_CRITICAL(&_mux);
    uint8_t target = (id == 0) ? _last_id : id;
    const TimerEntry* e = findEntry(target);
    uint32_t dur = e ? e->duration_seconds : _last_duration_seconds;
    DisplayFormat fmt = e ? e->format : _last_format;
    char label[TIMER_LABEL_MAX + 1] = {0};
    if (e) {
        memcpy(label, e->label, sizeof(label));
    }
    portEXIT_CRITICAL(&_mux);

    if (dur == 0) {
        return 0;
    }
    if (fmt == DisplayFormat::None) {
        fmt = DisplayFormat::Seconds;
    }
    return start(dur, label, fmt);
}

// ---------------------------------------------------------------------------
// Status queries
// ---------------------------------------------------------------------------

bool TimerManager::isRunning() const {
    portENTER_CRITICAL(&_mux);
    for (const auto& e : _entries) {
        if (e.running) {
            portEXIT_CRITICAL(&_mux);
            return true;
        }
    }
    portEXIT_CRITICAL(&_mux);
    return false;
}

bool TimerManager::isRunning(uint8_t id) const {
    portENTER_CRITICAL(&_mux);
    const TimerEntry* e = findEntry(id);
    bool running = e && e->running;
    portEXIT_CRITICAL(&_mux);
    return running;
}

std::vector<TimerManager::TimerEntry> TimerManager::listActive() const {
    portENTER_CRITICAL(&_mux);
    std::vector<TimerEntry> result;
    for (const auto& e : _entries) {
        if (e.running) {
            result.push_back(e);
        }
    }
    portEXIT_CRITICAL(&_mux);
    return result;
}

const TimerManager::TimerEntry* TimerManager::getEntry(uint8_t id) const {
    portENTER_CRITICAL(&_mux);
    const TimerEntry* e = findEntry(id);
    portEXIT_CRITICAL(&_mux);
    return e;
}

uint8_t TimerManager::lastTimerId() const {
    portENTER_CRITICAL(&_mux);
    uint8_t id = _last_id;
    portEXIT_CRITICAL(&_mux);
    return id;
}

uint32_t TimerManager::remainingSeconds() const {
    portENTER_CRITICAL(&_mux);
    const TimerEntry* soonest = soonestRunning();
    if (!soonest) {
        portEXIT_CRITICAL(&_mux);
        return 0;
    }
    uint64_t end_ms = soonest->end_ms;
    portEXIT_CRITICAL(&_mux);

    uint64_t now_ms = millis();
    if (now_ms >= end_ms) return 0;
    return static_cast<uint32_t>((end_ms - now_ms) / 1000ULL);
}

uint32_t TimerManager::durationSeconds() const {
    portENTER_CRITICAL(&_mux);
    const TimerEntry* soonest = soonestRunning();
    uint32_t dur = soonest ? soonest->duration_seconds : 0;
    portEXIT_CRITICAL(&_mux);
    return dur;
}

TimerManager::DisplayFormat TimerManager::displayFormat() const {
    portENTER_CRITICAL(&_mux);
    const TimerEntry* soonest = soonestRunning();
    DisplayFormat fmt = soonest ? soonest->format : DisplayFormat::None;
    portEXIT_CRITICAL(&_mux);
    return fmt;
}

uint32_t TimerManager::lastDurationSeconds() const {
    portENTER_CRITICAL(&_mux);
    uint32_t dur = _last_duration_seconds;
    portEXIT_CRITICAL(&_mux);
    return dur;
}

TimerManager::DisplayFormat TimerManager::lastDisplayFormat() const {
    portENTER_CRITICAL(&_mux);
    DisplayFormat fmt = _last_format;
    portEXIT_CRITICAL(&_mux);
    return fmt;
}

// ---------------------------------------------------------------------------
// Update (called from main loop)
// ---------------------------------------------------------------------------

void TimerManager::update() {
    std::vector<std::pair<uint8_t, std::string>> fired;

    portENTER_CRITICAL(&_mux);
    for (auto& e : _entries) {
        if (e.expired) {
            e.expired = false;
            fired.push_back({e.id, std::string(e.label)});
        }
    }
    portEXIT_CRITICAL(&_mux);

    ExpiredCallback cb;
    portENTER_CRITICAL(&_mux);
    cb = _expired_callback;
    portEXIT_CRITICAL(&_mux);

    for (auto& f : fired) {
        if (cb) {
            cb(f.first, f.second.c_str());
        }
    }
}

void TimerManager::setExpiredCallback(ExpiredCallback callback) {
    portENTER_CRITICAL(&_mux);
    _expired_callback = callback;
    portEXIT_CRITICAL(&_mux);
}

// ---------------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------------

void TimerManager::persistTimers(NVSStorage* storage) {
    if (!storage) return;
    if (!storage->beginTimers(false)) return;

    Preferences& p = storage->getTimersPrefs();

    portENTER_CRITICAL(&_mux);
    std::vector<TimerEntry> active;
    for (const auto& e : _entries) {
        if (e.running) active.push_back(e);
    }
    portEXIT_CRITICAL(&_mux);

    p.putUChar(KEY_COUNT, static_cast<uint8_t>(active.size()));
    for (size_t i = 0; i < active.size(); i++) {
        const auto& e = active[i];
        char key[12];

        snprintf(key, sizeof(key), "id%u", (unsigned)i);
        p.putUChar(key, e.id);

        snprintf(key, sizeof(key), "lbl%u", (unsigned)i);
        p.putString(key, e.label);

        snprintf(key, sizeof(key), "dur%u", (unsigned)i);
        p.putULong(key, e.duration_seconds);

        snprintf(key, sizeof(key), "end%u", (unsigned)i);
        p.putLong64(key, e.end_epoch_ms);

        snprintf(key, sizeof(key), "fmt%u", (unsigned)i);
        p.putUChar(key, static_cast<uint8_t>(e.format));
    }

    storage->endTimers();
}

void TimerManager::rehydrateTimers(NVSStorage* storage) {
    if (!storage) return;
    if (!storage->beginTimers(true)) return;

    Preferences& p = storage->getTimersPrefs();
    uint8_t count = p.getUChar(KEY_COUNT, 0);

    ClockSync cs;
    long long now_epoch = cs.epochMs();

    if (count == 0 || now_epoch <= 0) {
        storage->endTimers();
        return;
    }

    for (uint8_t i = 0; i < count && i < TIMER_MAX_ENTRIES; i++) {
        char key[12];

        snprintf(key, sizeof(key), "id%u", (unsigned)i);
        uint8_t id = p.getUChar(key, 0);

        snprintf(key, sizeof(key), "lbl%u", (unsigned)i);
        String label_str = p.getString(key, "");

        snprintf(key, sizeof(key), "dur%u", (unsigned)i);
        uint32_t dur = p.getULong(key, 0);

        snprintf(key, sizeof(key), "end%u", (unsigned)i);
        long long end_epoch = p.getLong64(key, 0);

        snprintf(key, sizeof(key), "fmt%u", (unsigned)i);
        uint8_t fmt_raw = p.getUChar(key, 0);
        DisplayFormat fmt = static_cast<DisplayFormat>(fmt_raw);

        if (id == 0 || dur == 0 || end_epoch <= now_epoch) {
            continue;
        }

        uint32_t remaining = static_cast<uint32_t>((end_epoch - now_epoch) / 1000LL);
        if (remaining == 0) {
            continue;
        }

        // start() will allocate a new id, but we preserve the label/format.
        // We can't guarantee the original id is reused, which is fine.
        (void)id;
        start(remaining, label_str.c_str(), fmt);

        // Fix up end_epoch_ms on the just-added entry to match original.
        portENTER_CRITICAL(&_mux);
        TimerEntry* e = findEntry(_last_id);
        if (e) {
            e->end_epoch_ms = end_epoch;
            // Adjust duration_seconds to original for display/repeat accuracy.
            e->duration_seconds = dur;
        }
        portEXIT_CRITICAL(&_mux);
    }

    storage->endTimers();
    ESP_LOGI(TAG, "Rehydrated timers from NVS (count=%u)", count);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TimerManager::onTimerExpired(void* arg) {
    auto* ctx = static_cast<CallbackArg*>(arg);
    if (ctx && ctx->manager) {
        ctx->manager->markExpired(ctx->id);
    }
}

void TimerManager::markExpired(uint8_t id) {
    portENTER_CRITICAL(&_mux);
    TimerEntry* e = findEntry(id);
    if (e) {
        e->running = false;
        e->expired = true;
        e->end_ms = 0;
    }
    portEXIT_CRITICAL(&_mux);
    ESP_LOGI(TAG, "Timer %u expired", id);
}

TimerManager::TimerEntry* TimerManager::findEntry(uint8_t id) {
    for (auto& e : _entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const TimerManager::TimerEntry* TimerManager::findEntry(uint8_t id) const {
    for (const auto& e : _entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const TimerManager::TimerEntry* TimerManager::soonestRunning() const {
    const TimerEntry* best = nullptr;
    for (const auto& e : _entries) {
        if (!e.running) continue;
        if (!best || e.end_ms < best->end_ms) {
            best = &e;
        }
    }
    return best;
}

uint8_t TimerManager::allocId() {
    // IDs 1-255; skip any currently in _entries.
    for (uint16_t candidate = 1; candidate <= 255; candidate++) {
        bool used = false;
        for (const auto& e : _entries) {
            if (e.id == static_cast<uint8_t>(candidate)) {
                used = true;
                break;
            }
        }
        if (!used) return static_cast<uint8_t>(candidate);
    }
    return 0;
}
