/**
 * TimerManager.cpp
 *
 * Implementation for TimerManager.
 */

#include "TimerManager.h"

#include <esp_log.h>

static const char* TAG = "TimerManager";

TimerManager::TimerManager()
    : _timer(nullptr)
    , _expired_callback(nullptr)
    , _timer_mux(portMUX_INITIALIZER_UNLOCKED)
    , _running(false)
    , _expired(false)
    , _duration_seconds(0)
    , _last_duration_seconds(0)
    , _end_ms(0)
    , _display_format(DisplayFormat::None)
    , _last_display_format(DisplayFormat::None) {
}

TimerManager::~TimerManager() {
    if (_timer) {
        esp_timer_stop(_timer);
        esp_timer_delete(_timer);
        _timer = nullptr;
    }
}

bool TimerManager::start(uint32_t duration_seconds) {
    return start(duration_seconds, DisplayFormat::Seconds);
}

bool TimerManager::start(uint32_t duration_seconds, DisplayFormat format) {
    if (duration_seconds == 0) {
        return false;
    }

    portENTER_CRITICAL(&_timer_mux);
    if (_running) {
        portEXIT_CRITICAL(&_timer_mux);
        return false;
    }
    _running = true;
    _expired = false;
    _duration_seconds = duration_seconds;
    _last_duration_seconds = duration_seconds;
    _display_format = format;
    _last_display_format = format;
    _end_ms = millis() + (static_cast<uint64_t>(duration_seconds) * 1000ULL);
    portEXIT_CRITICAL(&_timer_mux);

    if (_timer == nullptr) {
        esp_timer_create_args_t timer_args = {};
        timer_args.callback = &TimerManager::onTimerExpired;
        timer_args.arg = this;
        timer_args.dispatch_method = ESP_TIMER_TASK;
        timer_args.name = "timer_manager";
        esp_err_t create_err = esp_timer_create(&timer_args, &_timer);
        if (create_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(create_err));
            portENTER_CRITICAL(&_timer_mux);
            _running = false;
            _duration_seconds = 0;
            _display_format = DisplayFormat::None;
            _end_ms = 0;
            portEXIT_CRITICAL(&_timer_mux);
            return false;
        }
    }

    esp_err_t start_err = esp_timer_start_once(
        _timer,
        static_cast<uint64_t>(duration_seconds) * 1000000ULL
    );
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(start_err));
        portENTER_CRITICAL(&_timer_mux);
        _running = false;
        _duration_seconds = 0;
        _display_format = DisplayFormat::None;
        _end_ms = 0;
        portEXIT_CRITICAL(&_timer_mux);
        return false;
    }

    ESP_LOGI(TAG, "Timer started (%u seconds)", duration_seconds);
    return true;
}

bool TimerManager::cancel() {
    portENTER_CRITICAL(&_timer_mux);
    bool was_running = _running;
    _running = false;
    _expired = false;
    _duration_seconds = 0;
    _display_format = DisplayFormat::None;
    _end_ms = 0;
    portEXIT_CRITICAL(&_timer_mux);

    if (!was_running) {
        return false;
    }

    if (_timer) {
        esp_timer_stop(_timer);
    }

    ESP_LOGI(TAG, "Timer canceled");
    return true;
}

bool TimerManager::repeat() {
    uint32_t last_duration = lastDurationSeconds();
    if (last_duration == 0) {
        return false;
    }

    DisplayFormat last_format = lastDisplayFormat();
    if (last_format == DisplayFormat::None) {
        last_format = DisplayFormat::Seconds;
    }
    return start(last_duration, last_format);
}

bool TimerManager::isRunning() const {
    portENTER_CRITICAL(&_timer_mux);
    bool running = _running;
    portEXIT_CRITICAL(&_timer_mux);
    return running;
}

uint32_t TimerManager::durationSeconds() const {
    portENTER_CRITICAL(&_timer_mux);
    uint32_t duration = _duration_seconds;
    portEXIT_CRITICAL(&_timer_mux);
    return duration;
}

uint32_t TimerManager::remainingSeconds() const {
    portENTER_CRITICAL(&_timer_mux);
    bool running = _running;
    uint64_t end_ms = _end_ms;
    portEXIT_CRITICAL(&_timer_mux);

    if (!running || end_ms == 0) {
        return 0;
    }

    uint64_t now_ms = millis();
    if (now_ms >= end_ms) {
        return 0;
    }

    return static_cast<uint32_t>((end_ms - now_ms) / 1000ULL);
}

uint32_t TimerManager::lastDurationSeconds() const {
    portENTER_CRITICAL(&_timer_mux);
    uint32_t last_duration = _last_duration_seconds;
    portEXIT_CRITICAL(&_timer_mux);
    return last_duration;
}

TimerManager::DisplayFormat TimerManager::displayFormat() const {
    portENTER_CRITICAL(&_timer_mux);
    DisplayFormat format = _display_format;
    portEXIT_CRITICAL(&_timer_mux);
    return format;
}

TimerManager::DisplayFormat TimerManager::lastDisplayFormat() const {
    portENTER_CRITICAL(&_timer_mux);
    DisplayFormat format = _last_display_format;
    portEXIT_CRITICAL(&_timer_mux);
    return format;
}

void TimerManager::update() {
    ExpiredCallback callback;
    bool expired = false;

    portENTER_CRITICAL(&_timer_mux);
    if (_expired) {
        expired = true;
        _expired = false;
        callback = _expired_callback;
    }
    portEXIT_CRITICAL(&_timer_mux);

    if (expired && callback) {
        callback();
    }
}

void TimerManager::setExpiredCallback(ExpiredCallback callback) {
    portENTER_CRITICAL(&_timer_mux);
    _expired_callback = callback;
    portEXIT_CRITICAL(&_timer_mux);
}

void TimerManager::onTimerExpired(void* arg) {
    auto* instance = static_cast<TimerManager*>(arg);
    if (instance) {
        instance->markExpired();
    }
}

void TimerManager::markExpired() {
    portENTER_CRITICAL(&_timer_mux);
    _running = false;
    _expired = true;
    _duration_seconds = 0;
    _display_format = DisplayFormat::None;
    _end_ms = 0;
    portEXIT_CRITICAL(&_timer_mux);
    ESP_LOGI(TAG, "Timer expired");
}
