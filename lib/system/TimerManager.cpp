/**
 * TimerManager.cpp
 *
 * Implementation for TimerManager.
 */

#include "TimerManager.h"
#include "NvsStorage.h"
#include "ClockSync.h"

#include <esp_log.h>
#include <cstring>
#include <new>
#include <string>

static const char* TAG = "TimerManager";

#define NS_TIMERS   "timers"
#define KEY_COUNT   "count"

namespace {
// RAII guard for the FreeRTOS mutex. Safe to hold across heap allocation.
struct Lock {
    SemaphoreHandle_t m;
    explicit Lock(SemaphoreHandle_t mm) : m(mm) {
        if (m) xSemaphoreTake(m, portMAX_DELAY);
    }
    ~Lock() {
        if (m) xSemaphoreGive(m);
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};
}  // namespace

TimerManager::TimerManager()
    : _expired_callback(nullptr)
    , _mutex(xSemaphoreCreateMutex())
    , _last_id(0)
    , _last_duration_seconds(0)
    , _last_format(DisplayFormat::None) {
    _last_label[0] = '\0';
    // Reserve well above the active cap so push_back never reallocates in
    // practice (8 running + up to 8 expiry-pending entries). With a FreeRTOS
    // mutex a realloc would be safe anyway, but this avoids the churn.
    _entries.reserve(2 * TIMER_MAX_ENTRIES);
}

TimerManager::~TimerManager() {
    std::vector<TimerEntry> doomed;
    {
        Lock l(_mutex);
        doomed.swap(_entries);
    }
    for (auto& e : doomed) {
        destroyResources(e.esp_timer, e.cb_arg, /*stop_first=*/true);
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
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

    uint8_t id = 0;
    CallbackArg* arg = nullptr;
    esp_timer_handle_t handle = nullptr;

    {
        Lock l(_mutex);

        size_t active = 0;
        for (const auto& e : _entries) {
            if (e.running) active++;
        }
        if (active >= TIMER_MAX_ENTRIES) {
            ESP_LOGW(TAG, "Max timers reached");
            return 0;
        }

        id = allocId();
        if (id == 0) {
            return 0;
        }

        // Heap-allocate the callback arg so its address is stable for the
        // lifetime of the esp_timer, regardless of _entries reallocation.
        arg = new (std::nothrow) CallbackArg{this, id};
        if (!arg) {
            return 0;
        }

        esp_timer_create_args_t args = {};
        args.callback = &TimerManager::onTimerExpired;
        args.arg = arg;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "timer_mgr";
        if (esp_timer_create(&args, &handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create failed for id %u", id);
            delete arg;
            return 0;
        }

        if (esp_timer_start_once(handle, static_cast<uint64_t>(duration_seconds) * 1000000ULL) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_start_once failed for id %u", id);
            esp_timer_delete(handle);
            delete arg;
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
        entry.cb_arg = arg;

        _entries.push_back(entry);

        _last_id = id;
        _last_duration_seconds = duration_seconds;
        _last_format = format;
        memcpy(_last_label, entry.label, sizeof(_last_label));

        ESP_LOGI(TAG, "Timer %u started (%u s, label='%s')", id, duration_seconds, entry.label);
    }

    return id;
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

bool TimerManager::cancel(uint8_t id) {
    esp_timer_handle_t handle = nullptr;
    void* arg = nullptr;
    uint8_t target = 0;

    {
        Lock l(_mutex);
        target = (id == 0) ? _last_id : id;
        for (auto it = _entries.begin(); it != _entries.end(); ++it) {
            if (it->id == target && it->running) {
                // Preserve enough state for a later repeat(0).
                _last_duration_seconds = it->duration_seconds;
                _last_format = it->format;
                memcpy(_last_label, it->label, sizeof(_last_label));
                handle = it->esp_timer;
                arg = it->cb_arg;
                _entries.erase(it);
                break;
            }
        }
    }

    if (!handle && !arg) {
        return false;
    }

    destroyResources(handle, arg, /*stop_first=*/true);
    ESP_LOGI(TAG, "Timer %u canceled", target);
    return true;
}

// ---------------------------------------------------------------------------
// Repeat
// ---------------------------------------------------------------------------

uint8_t TimerManager::repeatTimer(uint8_t id) {
    uint32_t dur = 0;
    DisplayFormat fmt = DisplayFormat::Seconds;
    char label[TIMER_LABEL_MAX + 1] = {0};

    {
        Lock l(_mutex);
        uint8_t target = (id == 0) ? _last_id : id;
        const TimerEntry* e = findEntry(target);
        if (e) {
            dur = e->duration_seconds;
            fmt = e->format;
            memcpy(label, e->label, sizeof(label));
        } else {
            dur = _last_duration_seconds;
            fmt = _last_format;
            memcpy(label, _last_label, sizeof(label));
        }
    }

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
    Lock l(_mutex);
    for (const auto& e : _entries) {
        if (e.running) return true;
    }
    return false;
}

bool TimerManager::isRunning(uint8_t id) const {
    Lock l(_mutex);
    const TimerEntry* e = findEntry(id);
    return e && e->running;
}

std::vector<TimerManager::TimerEntry> TimerManager::listActive() const {
    Lock l(_mutex);
    std::vector<TimerEntry> result;
    for (const auto& e : _entries) {
        if (e.running) result.push_back(e);
    }
    return result;
}

const TimerManager::TimerEntry* TimerManager::getEntry(uint8_t id) const {
    Lock l(_mutex);
    return findEntry(id);
}

uint8_t TimerManager::lastTimerId() const {
    Lock l(_mutex);
    return _last_id;
}

uint32_t TimerManager::remainingSeconds() const {
    uint64_t end_ms = 0;
    {
        Lock l(_mutex);
        const TimerEntry* soonest = soonestRunning();
        if (!soonest) return 0;
        end_ms = soonest->end_ms;
    }
    uint64_t now_ms = millis();
    if (now_ms >= end_ms) return 0;
    return static_cast<uint32_t>((end_ms - now_ms) / 1000ULL);
}

uint32_t TimerManager::durationSeconds() const {
    Lock l(_mutex);
    const TimerEntry* soonest = soonestRunning();
    return soonest ? soonest->duration_seconds : 0;
}

TimerManager::DisplayFormat TimerManager::displayFormat() const {
    Lock l(_mutex);
    const TimerEntry* soonest = soonestRunning();
    return soonest ? soonest->format : DisplayFormat::None;
}

uint32_t TimerManager::lastDurationSeconds() const {
    Lock l(_mutex);
    return _last_duration_seconds;
}

TimerManager::DisplayFormat TimerManager::lastDisplayFormat() const {
    Lock l(_mutex);
    return _last_format;
}

// ---------------------------------------------------------------------------
// Update (called from main loop) - delivers callbacks and reaps fired timers
// ---------------------------------------------------------------------------

void TimerManager::update() {
    struct Fired {
        uint8_t id;
        std::string label;
        esp_timer_handle_t handle;
        void* arg;
    };
    std::vector<Fired> fired;
    ExpiredCallback cb;

    {
        Lock l(_mutex);
        cb = _expired_callback;
        for (auto it = _entries.begin(); it != _entries.end();) {
            if (it->expired) {
                fired.push_back({it->id, std::string(it->label), it->esp_timer, it->cb_arg});
                it = _entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Outside the lock: fire callbacks, then release the OS/heap resources of
    // the (already-fired, already-stopped) one-shot timers.
    for (auto& f : fired) {
        if (cb) {
            cb(f.id, f.label.c_str());
        }
        destroyResources(f.handle, f.arg, /*stop_first=*/false);
    }
}

void TimerManager::setExpiredCallback(ExpiredCallback callback) {
    Lock l(_mutex);
    _expired_callback = callback;
}

// ---------------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------------

void TimerManager::persistTimers(NVSStorage* storage) {
    if (!storage) return;

    std::vector<TimerEntry> active;
    {
        Lock l(_mutex);
        for (const auto& e : _entries) {
            if (e.running) active.push_back(e);
        }
    }

    if (!storage->beginTimers(false)) return;
    Preferences& p = storage->getTimersPrefs();

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

        // start() assigns a fresh id; preserve the saved label/format and fix
        // up the original epoch deadline so the countdown stays accurate.
        uint8_t new_id = start(remaining, label_str.c_str(), fmt);
        if (new_id == 0) {
            continue;
        }

        Lock l(_mutex);
        TimerEntry* e = findEntry(new_id);
        if (e) {
            e->end_epoch_ms = end_epoch;
            e->duration_seconds = dur;
        }
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
    Lock l(_mutex);
    TimerEntry* e = findEntry(id);
    if (e) {
        e->running = false;
        e->expired = true;
        e->end_ms = 0;
    }
    ESP_LOGI(TAG, "Timer %u expired", id);
}

void TimerManager::destroyResources(esp_timer_handle_t handle, void* cb_arg, bool stop_first) {
    if (handle) {
        if (stop_first) {
            esp_timer_stop(handle);
        }
        esp_timer_delete(handle);
    }
    delete static_cast<CallbackArg*>(cb_arg);
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

uint8_t TimerManager::allocId() const {
    // IDs 1-255; skip any currently present in _entries (caller holds _mutex).
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
