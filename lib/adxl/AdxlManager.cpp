/**
 * @file AdxlManager.cpp
 * @brief Motion state management for ADXL345 accelerometer
 */

#include "AdxlManager.h"
#include "Adxl345.h"
#include "ArduinoSSD1351.h"
#include "DeviceConfig.h"
#include "DeviceSimulator.h"
#include "GifPlayer.h"
#include "NvsStorage.h"
#include "Axp2101.h"
#include "WifiManager.h"
#include "HapticsManager.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <math.h>

static const char* TAG = "AdxlManager";

namespace {
constexpr float SHAKE_THRESHOLD = 8.0f;
constexpr float INACTIVITY_THRESHOLD = 1.5f;
constexpr float TILT_THRESHOLD = 9.0f;
constexpr float HALF_TILT_THRESHOLD = 4.2f;
constexpr float FLIP_THRESHOLD = -8.0f;
constexpr float ACCELERATION_THRESHOLD = 6.0f;
constexpr float ACCELERATION_CHANGE_THRESHOLD = 4.0f;

constexpr unsigned long INACTIVITY_TIMEOUT_MS = AdxlManager::toMillis(0, 25, 0);
constexpr unsigned long DISPLAY_TIMEOUT_MS = AdxlManager::toMillis(0, 15, 0);
constexpr unsigned long ENTER_LIGHT_SLEEP_TIMER_MS = AdxlManager::toMillis(0, 15, 0);
constexpr unsigned long UI_GRACE_MS = 1500;
constexpr unsigned long INTERACTION_LOCKOUT_MS = 500;
constexpr unsigned long SUDDEN_ACCEL_LOCKOUT_MS = 600;
constexpr unsigned long BUTTON_WAKE_LOCKOUT_MS = 800;
constexpr unsigned long LIFT_ARM_DELAY_MS = 800;
constexpr unsigned long TAP_STATE_MS = 800;
constexpr unsigned long DOUBLE_TAP_STATE_MS = 800;
constexpr unsigned long SUDDEN_ACCEL_STATE_MS = 800;
constexpr unsigned long LIFT_STATE_MS = 800;
constexpr unsigned long SHAKE_STATE_MS = 600;

constexpr uint8_t INT_SOURCE_SINGLE_TAP = 0x40;
constexpr uint8_t INT_SOURCE_DOUBLE_TAP = 0x20;
} // namespace

AdxlManager::AdxlManager(Adxl345* adxl,
                         HapticsManager* haptics,
                         ArduinoSSD1351* display,
                         WifiManager* wifi_manager,
                         NVSStorage* storage,
                         AXP2101* power_manager,
                         unsigned long light_sleep_interval_ms)
    : _adxl(adxl)
    , _display(display)
    , _gif_player(nullptr)
    , _haptics(haptics)
    , _power_manager(power_manager)
    , _wifi_manager(wifi_manager)
    , _storage(storage)
    , _states{}
    , _state_last_set_ms{}
    , _inactivity_start_ms(0)
    , _interaction_lockout_ms(0)
    , _sudden_accel_lockout_ms(0)
    , _last_shake_haptic_ms(0)
    , _orientation_haptic_ms{}
    , _display_dimmed(false)
    , _display_dim_reason(DisplayDimReason::NONE)
    , _display_last_activity_ms(0)
    , _light_sleep_interval_ms(light_sleep_interval_ms)
    , _light_sleep_armed_ms(0)
    , _sleep_pending_ms(0)
    , _sleep_wifi_stopped(false)
    , _button_wake_lockout_ms(0)
    , _lift_haptic_armed(false)
    , _timer_active_provider(nullptr)
    , _inactivity_allowed_provider(nullptr) {
}

void AdxlManager::update() {
    if (!_adxl || !_adxl->isReady()) {
        return;
    }

    expireTransientStates();

    if (_sleep_pending_ms > 0 && millis() >= _sleep_pending_ms) {
        _sleep_pending_ms = 0;
        enterLightSleep();
        return;
    }

    uint8_t samples = _adxl->getFifoSampleCount();
    if (samples == 0) {
        return;
    }

    detectShakes(samples);
    if (!checkMotionState(MotionState::SHAKING)) {
        detectTapping();
        detectInactivity(samples);
        autoDimDisplay(samples);
    }

    detectSuddenAcceleration(samples);
    detectOrientation(samples);
}

bool AdxlManager::checkMotionState(MotionState state) const {
    return _states[static_cast<size_t>(state)];
}

void AdxlManager::resetMotionStates() {
    _states.fill(false);
}

bool AdxlManager::motionInteracted() const {
    return checkMotionState(MotionState::SHAKING) ||
           checkMotionState(MotionState::TAPPED) ||
           checkMotionState(MotionState::DOUBLE_TAPPED) ||
           checkMotionState(MotionState::SUDDEN_ACCELERATION);
}

bool AdxlManager::motionOriented() const {
    return checkMotionState(MotionState::TILTED_LEFT) ||
           checkMotionState(MotionState::TILTED_RIGHT) ||
           checkMotionState(MotionState::HALF_TILTED_LEFT) ||
           checkMotionState(MotionState::HALF_TILTED_RIGHT) ||
           checkMotionState(MotionState::UPSIDE_DOWN);
}

void AdxlManager::setGifPlayer(GifPlayer* gif_player) {
    _gif_player = gif_player;
}

bool AdxlManager::shouldIgnoreButton() const {
    if (_button_wake_lockout_ms == 0) {
        return false;
    }
    return (millis() - _button_wake_lockout_ms) < BUTTON_WAKE_LOCKOUT_MS;
}

void AdxlManager::wakeFromSleep() {
    _inactivity_start_ms = 0;
    _light_sleep_armed_ms = 0;
    _sleep_pending_ms = 0;
    if (_sleep_wifi_stopped && _wifi_manager) {
        _wifi_manager->start();
        ESP_LOGI(TAG, "Wake: restarting access point (pre-sleep cancel)");
        _wifi_manager->startAccessPoint();
        _wifi_manager->reconnect();
    }
    _sleep_wifi_stopped = false;
    _lift_haptic_armed = false;
    setDisplayDimmed(false, DisplayDimReason::SLEEP);
    setMotionState(MotionState::LIGHT_SLEEP, false);
}

void AdxlManager::setMotionState(MotionState state, bool value) {
    size_t index = static_cast<size_t>(state);
    _states[index] = value;
    if (value) {
        _state_last_set_ms[index] = millis();
    }
    _adxl->clearInterrupts();
}

void AdxlManager::expireTransientStates() {
    unsigned long now = millis();
    auto expire = [&](MotionState state, unsigned long ttl_ms) {
        size_t index = static_cast<size_t>(state);
        unsigned long set_ms = _state_last_set_ms[index];
        if (set_ms > 0 && now - set_ms >= ttl_ms) {
            _states[index] = false;
            _state_last_set_ms[index] = 0;
        }
    };

    expire(MotionState::TAPPED, TAP_STATE_MS);
    expire(MotionState::DOUBLE_TAPPED, DOUBLE_TAP_STATE_MS);
    expire(MotionState::SUDDEN_ACCELERATION, SUDDEN_ACCEL_STATE_MS);
    expire(MotionState::LIFTED, LIFT_STATE_MS);
    expire(MotionState::SHAKING, SHAKE_STATE_MS);
}

void AdxlManager::detectShakes(uint8_t samples) {
    if (samples == 0) {
        return;
    }

    if (checkMotionState(MotionState::TAPPED) ||
        checkMotionState(MotionState::DOUBLE_TAPPED) ||
        checkMotionState(MotionState::SUDDEN_ACCELERATION)) {
        _interaction_lockout_ms = millis();
        return;
    }

    if (millis() - _interaction_lockout_ms < INTERACTION_LOCKOUT_MS) {
        return;
    }

    float total_magnitude = 0.0f;
    for (uint8_t i = 0; i < samples; i++) {
        sensors_event_t event{};
        if (_adxl->getEvent(&event)) {
            total_magnitude += _adxl->calculateCombinedMagnitude(
                event.acceleration.x,
                event.acceleration.y,
                event.acceleration.z);
        }
    }

    float avg_magnitude = total_magnitude / samples;
    if (avg_magnitude >= SHAKE_THRESHOLD) {
        setMotionState(MotionState::SHAKING, true);
        if (canPlayHaptic()) {
            if (millis() - _last_shake_haptic_ms > 500) {
                _haptics->playEffect(HAPTIC_STRONG_BUZZ_100);
                _last_shake_haptic_ms = millis();
            }
        }
    }
}

void AdxlManager::detectTapping() {
    uint8_t int_source = _adxl->readRegister(ADXL345_REG_INT_SOURCE);
    if (!(int_source & (INT_SOURCE_SINGLE_TAP | INT_SOURCE_DOUBLE_TAP))) {
        return;
    }

    if (int_source & INT_SOURCE_DOUBLE_TAP) {
        setMotionState(MotionState::DOUBLE_TAPPED, true);
        if (canPlayHaptic()) {
            _haptics->playEffect(HAPTIC_DOUBLE_CLICK_100);
        }
        return;
    }

    if (int_source & INT_SOURCE_SINGLE_TAP) {
        setMotionState(MotionState::TAPPED, true);
        if (canPlayHaptic()) {
            _haptics->playEffect(HAPTIC_SHARP_CLICK_100);
        }
    }
}

void AdxlManager::detectSuddenAcceleration(uint8_t samples) {
    if (samples < 2) {
        return;
    }

    if (checkMotionState(MotionState::DOUBLE_TAPPED) ||
        checkMotionState(MotionState::TAPPED) ||
        checkMotionState(MotionState::SHAKING)) {
        _sudden_accel_lockout_ms = millis();
        return;
    }

    if (millis() - _sudden_accel_lockout_ms < SUDDEN_ACCEL_LOCKOUT_MS) {
        return;
    }

    static float prev_magnitude = 0.0f;
    sensors_event_t event{};
    if (!_adxl->getEvent(&event)) {
        return;
    }

    float current_magnitude = _adxl->calculateCombinedMagnitude(
        event.acceleration.x,
        event.acceleration.y,
        event.acceleration.z);
    float magnitude_change = fabsf(current_magnitude - prev_magnitude);

    prev_magnitude = current_magnitude;

    if (current_magnitude >= ACCELERATION_THRESHOLD &&
        magnitude_change >= ACCELERATION_CHANGE_THRESHOLD) {
        setMotionState(MotionState::SUDDEN_ACCELERATION, true);
        if (canPlayHaptic()) {
            _haptics->playEffect(HAPTIC_ALERT_750MS);
        }
    }
}

void AdxlManager::detectOrientation(uint8_t samples) {
    if (samples == 0) {
        return;
    }

    float avg_x = 0.0f;
    float avg_y = 0.0f;
    float avg_z = 0.0f;

    for (uint8_t i = 0; i < samples; i++) {
        sensors_event_t event{};
        if (_adxl->getEvent(&event)) {
            avg_x += event.acceleration.x;
            avg_y += event.acceleration.y;
            avg_z += event.acceleration.z;
            if (DeviceSimulator::isAdxlDebugEnabled()) {
                ESP_LOGD(TAG, "Accel sample %u: x=%.2f y=%.2f z=%.2f",
                    static_cast<unsigned>(i + 1),
                    event.acceleration.x,
                    event.acceleration.y,
                    event.acceleration.z);
            }
        }
    }

    avg_x /= samples;
    avg_y /= samples;
    avg_z /= samples;

    if (DeviceSimulator::isAdxlDebugEnabled()) {
        ESP_LOGD(TAG, "Accel avg: x=%.2f y=%.2f z=%.2f", avg_x, avg_y, avg_z);
    }

    setMotionState(MotionState::UPSIDE_DOWN, false);
    setMotionState(MotionState::TILTED_LEFT, false);
    setMotionState(MotionState::TILTED_RIGHT, false);
    setMotionState(MotionState::HALF_TILTED_LEFT, false);
    setMotionState(MotionState::HALF_TILTED_RIGHT, false);

    if (avg_z <= FLIP_THRESHOLD) {
        setMotionState(MotionState::UPSIDE_DOWN, true);
        playOrientationHaptic(HAPTIC_RAMP_DOWN_LONG_SMOOTH_1_100, 200, 0);
    } else if (avg_y >= TILT_THRESHOLD) {
        setMotionState(MotionState::TILTED_RIGHT, true);
        //playOrientationHaptic(HAPTIC_STRONG_BUZZ_100, 200, 1);
    } else if (avg_y <= -TILT_THRESHOLD) {
        setMotionState(MotionState::TILTED_LEFT, true);
        //playOrientationHaptic(HAPTIC_STRONG_BUZZ_100, 200, 2);
    } else if (avg_y >= HALF_TILT_THRESHOLD && avg_y < TILT_THRESHOLD) {
        setMotionState(MotionState::HALF_TILTED_RIGHT, true);
        //playOrientationHaptic(HAPTIC_LONG_DOUBLE_SHARP_CLICK_STRONG_1_100, 150, 3);
    } else if (avg_y <= -HALF_TILT_THRESHOLD && avg_y > -TILT_THRESHOLD) {
        setMotionState(MotionState::HALF_TILTED_LEFT, true);
        //playOrientationHaptic(HAPTIC_LONG_DOUBLE_SHARP_CLICK_STRONG_1_100, 150, 4);
    } else if (avg_x >= HALF_TILT_THRESHOLD && avg_x < TILT_THRESHOLD) {
        setMotionState(MotionState::HALF_TILTED_RIGHT, true);
        //playOrientationHaptic(HAPTIC_LONG_DOUBLE_SHARP_CLICK_STRONG_1_100, 150, 5);
    } else if (avg_x <= -HALF_TILT_THRESHOLD && avg_x > -TILT_THRESHOLD) {
        setMotionState(MotionState::HALF_TILTED_LEFT, true);
        //playOrientationHaptic(HAPTIC_LONG_DOUBLE_SHARP_CLICK_STRONG_1_100, 150, 6);
    }
}

void AdxlManager::setTimerActiveProvider(std::function<bool()> provider) {
    _timer_active_provider = provider;
}

void AdxlManager::setInactivityAllowedProvider(std::function<bool()> provider) {
    _inactivity_allowed_provider = provider;
}

void AdxlManager::detectInactivity(uint8_t samples) {
    if (samples == 0) {
        return;
    }

    if (_inactivity_allowed_provider && !_inactivity_allowed_provider()) {
        _inactivity_start_ms = 0;
        _light_sleep_armed_ms = 0;
        _sleep_pending_ms = 0;
        _sleep_wifi_stopped = false;
        _lift_haptic_armed = false;
        setDisplayDimmed(false, DisplayDimReason::SLEEP);
        setMotionState(MotionState::LIGHT_SLEEP, false);
        return;
    }

    if (_timer_active_provider && _timer_active_provider()) {
        _inactivity_start_ms = 0;
        _light_sleep_armed_ms = 0;
        _sleep_pending_ms = 0;
        _sleep_wifi_stopped = false;
        _lift_haptic_armed = false;
        setDisplayDimmed(false, DisplayDimReason::SLEEP);
        setMotionState(MotionState::LIGHT_SLEEP, false);
        return;
    }

    float total_magnitude = 0.0f;
    for (uint8_t i = 0; i < samples; i++) {
        sensors_event_t event{};
        if (_adxl->getEvent(&event)) {
            total_magnitude += _adxl->calculateCombinedMagnitude(
                event.acceleration.x,
                event.acceleration.y,
                event.acceleration.z);
        }
    }

    float avg_magnitude = total_magnitude / samples;
    if (avg_magnitude < INACTIVITY_THRESHOLD) {
        if (_inactivity_start_ms == 0) {
            _inactivity_start_ms = millis();
        } else if (millis() - _inactivity_start_ms >= INACTIVITY_TIMEOUT_MS) {
            setMotionState(MotionState::LIGHT_SLEEP, true);
            if (_light_sleep_armed_ms == 0) {
                _light_sleep_armed_ms = millis();
                setDisplayDimmed(true, DisplayDimReason::SLEEP);
            } else if (millis() - _light_sleep_armed_ms >= ENTER_LIGHT_SLEEP_TIMER_MS) {
                scheduleLightSleep();
            }
        }
        if (!_lift_haptic_armed &&
            (millis() - _inactivity_start_ms) >= LIFT_ARM_DELAY_MS) {
            _lift_haptic_armed = true;
        }
    } else {
        if (_lift_haptic_armed && !motionInteracted()) {
            setMotionState(MotionState::LIFTED, true);
            if (canPlayHaptic()) {
                _haptics->playEffect(HAPTIC_STRONG_BUZZ_100);
            }
        }
        _lift_haptic_armed = false;
        _inactivity_start_ms = 0;
        _light_sleep_armed_ms = 0;
        _sleep_pending_ms = 0;
        _sleep_wifi_stopped = false;
        setDisplayDimmed(false, DisplayDimReason::SLEEP);
        setMotionState(MotionState::LIGHT_SLEEP, false);
    }
}

void AdxlManager::playOrientationHaptic(uint8_t effect_id, unsigned long cooldown_ms, size_t timer_id) {
    if (!canPlayHaptic() || timer_id >= _orientation_haptic_ms.size()) {
        return;
    }

    unsigned long now = millis();
    if (now - _orientation_haptic_ms[timer_id] > cooldown_ms) {
        _haptics->playEffect(effect_id);
        _orientation_haptic_ms[timer_id] = now;
    }
}

bool AdxlManager::canPlayHaptic() const {
    return _haptics && _haptics->isReady() && _haptics->isEnabled();
}

void AdxlManager::setDisplayDimmed(bool dimmed, DisplayDimReason reason) {
    if (!_display) {
        return;
    }

    if (dimmed) {
        if (!_display_dimmed) {
            _display->setBrightnessPercent(5);
            _display_dimmed = true;
        }
        if (reason != DisplayDimReason::NONE) {
            _display_dim_reason = reason;
        }
        return;
    }

    if (!_display_dimmed) {
        return;
    }

    if (reason != DisplayDimReason::NONE && _display_dim_reason != reason) {
        return;
    }

    _display->setBrightnessPercent(getStoredBrightness());
    _display_dimmed = false;
    _display_dim_reason = DisplayDimReason::NONE;
}

uint8_t AdxlManager::getStoredBrightness() const {
    if (_storage) {
        system_settings_t settings{};
        if (_storage->loadSystemSettings(&settings)) {
            return settings.brightness;
        }
    }
    return 100;
}

void AdxlManager::autoDimDisplay(uint8_t samples) {
    if (!_display || samples == 0) {
        return;
    }

    if (_inactivity_allowed_provider && !_inactivity_allowed_provider()) {
        _display_last_activity_ms = millis();
        setDisplayDimmed(false, DisplayDimReason::AUTO);
        setMotionState(MotionState::SLEEP, false);
        return;
    }

    float total_magnitude = 0.0f;
    for (uint8_t i = 0; i < samples; i++) {
        sensors_event_t event{};
        if (_adxl->getEvent(&event)) {
            total_magnitude += _adxl->calculateCombinedMagnitude(
                event.acceleration.x,
                event.acceleration.y,
                event.acceleration.z);
        }
    }

    float avg_magnitude = total_magnitude / samples;
    unsigned long now = millis();

    if (avg_magnitude >= INACTIVITY_THRESHOLD) {
        _display_last_activity_ms = now;
        setDisplayDimmed(false, DisplayDimReason::AUTO);
        setMotionState(MotionState::SLEEP, false);
        return;
    }

    if (_display_last_activity_ms == 0) {
        _display_last_activity_ms = now;
        return;
    }

    if (!_display_dimmed && now - _display_last_activity_ms >= DISPLAY_TIMEOUT_MS) {
        setDisplayDimmed(true, DisplayDimReason::AUTO);
        setMotionState(MotionState::SLEEP, true);
    }
}

void AdxlManager::enterLightSleep() {
    setMotionState(MotionState::SLEEP, true);
    if (!_sleep_wifi_stopped && _wifi_manager) {
        _wifi_manager->disconnect();
        _wifi_manager->stop();
        _sleep_wifi_stopped = true;
        _sleep_pending_ms = millis() + UI_GRACE_MS;
        ESP_LOGI(TAG, "WiFi stopped, delaying sleep %lu ms for UI refresh", UI_GRACE_MS);
        return;
    }
    ESP_LOGI(TAG, "Preparing light sleep (light_sleep_interval_ms=%lu)", _light_sleep_interval_ms);
    if (_gif_player) {
        _gif_player->renderFirstFrame();
    }
    if (_light_sleep_interval_ms == 0) {
        gpio_num_t irq_pin = static_cast<gpio_num_t>(AXP2101_IRQ_PIN);
        pinMode(AXP2101_IRQ_PIN, INPUT_PULLUP);
        int irq_level = gpio_get_level(irq_pin);
        bool rtc_valid = esp_sleep_is_valid_wakeup_gpio(irq_pin);
        ESP_LOGI(TAG, "Sleep IRQ pin=%d level=%d rtc_valid=%d",
                 static_cast<int>(irq_pin), irq_level, rtc_valid ? 1 : 0);
        if (_power_manager) {
            _power_manager->clearIrqStatus();
        }
        if (rtc_valid) {
            rtc_gpio_pullup_en(irq_pin);
            rtc_gpio_pulldown_dis(irq_pin);
            if (esp_sleep_enable_ext0_wakeup(irq_pin, 0) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable ext0 wake on button");
                return;
            }
            ESP_LOGI(TAG, "Entering light sleep (ext0 wake on button)");
        } else {
            if (gpio_wakeup_enable(irq_pin, GPIO_INTR_LOW_LEVEL) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable GPIO wake on button");
                return;
            }
            if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable light sleep GPIO wake");
                return;
            }
            ESP_LOGI(TAG, "Entering light sleep (GPIO wake on button)");
        }
    } else {
        if (esp_sleep_enable_timer_wakeup(_light_sleep_interval_ms * 1000ULL) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to enable light sleep timer");
            return;
        }
        ESP_LOGI(TAG, "Entering light sleep (%lu ms)", _light_sleep_interval_ms);
    }

    ESP_LOGI(TAG, "Calling esp_light_sleep_start()");
    Serial.flush();
    esp_err_t err = esp_light_sleep_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Light sleep start failed: %s", esp_err_to_name(err));
        return;
    }

    (void)esp_sleep_get_wakeup_cause();

    if (_light_sleep_interval_ms == 0) {
        gpio_num_t irq_pin = static_cast<gpio_num_t>(AXP2101_IRQ_PIN);
        if (esp_sleep_is_valid_wakeup_gpio(irq_pin)) {
            rtc_gpio_deinit(irq_pin);
        }
    }

    if (_wifi_manager) {
        _wifi_manager->start();
        ESP_LOGI(TAG, "Wake: restarting access point");
        _wifi_manager->restartAccessPoint();
        _wifi_manager->reconnect();
    }
    if (_display) {
        setDisplayDimmed(false, DisplayDimReason::NONE);
        _display_last_activity_ms = millis();
        ESP_LOGI(TAG, "Wake: restoring display brightness and resetting dim timer");
    }
    if (_gif_player) {
        _gif_player->resumePlayback();
    }
    _button_wake_lockout_ms = millis();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    _light_sleep_armed_ms = 0;
    _inactivity_start_ms = 0;
    _sleep_wifi_stopped = false;
}

void AdxlManager::scheduleLightSleep() {
    if (_sleep_pending_ms == 0) {
        _sleep_pending_ms = millis();
        ESP_LOGI(TAG, "Light sleep scheduled");
    }
}
