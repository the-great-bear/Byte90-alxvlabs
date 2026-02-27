/**
 * @file AdxlManager.h
 * @brief Motion state management for ADXL345 accelerometer
 *
 * Tracks motion states such as shaking, tapping, orientation, and inactivity
 * using data from the ADXL345 sensor.
 */

#pragma once

#include <Arduino.h>
#include <array>
#include <functional>

class Adxl345;
class ArduinoSSD1351;
class GifPlayer;
class HapticsManager;
class AXP2101;
class WifiManager;
class NVSStorage;

/**
 * @brief MotionState.
 */
enum class MotionState : uint8_t {
    SHAKING = 0,
    TAPPED,
    DOUBLE_TAPPED,
    SUDDEN_ACCELERATION,
    LIFTED,
    TILTED_LEFT,
    TILTED_RIGHT,
    HALF_TILTED_LEFT,
    HALF_TILTED_RIGHT,
    UPSIDE_DOWN,
    SLEEP,
    LIGHT_SLEEP,
    STATE_COUNT
};

enum class DisplayDimReason : uint8_t {
    NONE = 0,
    AUTO,
    SLEEP
};

/**
 * @brief AdxlManager.
 */
class AdxlManager {
public:
    /**
     * @brief Construct motion manager
     *
     * @param adxl Pointer to initialized ADXL345 wrapper
     * @param haptics Pointer to haptics manager (optional)
     */
    AdxlManager(Adxl345* adxl,
                HapticsManager* haptics,
                ArduinoSSD1351* display,
                WifiManager* wifi_manager,
                NVSStorage* storage,
                AXP2101* power_manager,
                unsigned long light_sleep_interval_ms);

    /**
     * @brief Update motion detection based on current sensor data
     */
    void update();

    /**
     * @brief Check if a motion state is active
     *
     * @param state Motion state to query
     * @return true if active, false otherwise
     */
    bool checkMotionState(MotionState state) const;

    /**
     * @brief Reset all motion states
     */
    void resetMotionStates();

    /**
     * @brief Check if any interaction states are active
     *
     * @return true if shaking/tapping/sudden acceleration is active
     */
    bool motionInteracted() const;

    /**
     * @brief Check if any orientation states are active
     *
     * @return true if orientation differs from normal
     */
    bool motionOriented() const;

    /**
     * @brief Provide GIF player for pre-sleep rendering
     *
     * @param gif_player Pointer to active GifPlayer (optional)
     */
    void setGifPlayer(GifPlayer* gif_player);

    /**
     * @brief Provide a callback to determine if a timer is active
     *
     * @param provider Returns true when a timer is running
     */
    void setTimerActiveProvider(std::function<bool()> provider);

    /**
     * @brief Provide a callback to allow inactivity handling
     *
     * @param provider Returns true when inactivity checks should run
     */
    void setInactivityAllowedProvider(std::function<bool()> provider);

    /**
     * @brief Check if button clicks should be ignored after wake
     */
    bool shouldIgnoreButton() const;
    void wakeFromSleep();

    static constexpr uint64_t toMillis(uint64_t hours, uint64_t minutes, uint64_t seconds) {
        return ((hours * 3600ULL) + (minutes * 60ULL) + seconds) * 1000ULL;
    }

private:
    void setMotionState(MotionState state, bool value);
    void expireTransientStates();
    void detectShakes(uint8_t samples);
    void detectTapping();
    void detectSuddenAcceleration(uint8_t samples);
    void detectOrientation(uint8_t samples);
    void detectInactivity(uint8_t samples);
    void autoDimDisplay(uint8_t samples);
    void playOrientationHaptic(uint8_t effect_id, unsigned long cooldown_ms, size_t timer_id);
    bool canPlayHaptic() const;
    void setDisplayDimmed(bool dimmed, DisplayDimReason reason);
    uint8_t getStoredBrightness() const;
    void enterLightSleep();
    void scheduleLightSleep();

    Adxl345* _adxl;
    ArduinoSSD1351* _display;
    GifPlayer* _gif_player;
    HapticsManager* _haptics;
    AXP2101* _power_manager;
    WifiManager* _wifi_manager;
    NVSStorage* _storage;
    std::array<bool, static_cast<size_t>(MotionState::STATE_COUNT)> _states;
    std::array<unsigned long, static_cast<size_t>(MotionState::STATE_COUNT)> _state_last_set_ms;
    unsigned long _inactivity_start_ms;
    unsigned long _interaction_lockout_ms;
    unsigned long _sudden_accel_lockout_ms;
    unsigned long _last_shake_haptic_ms;
    std::array<unsigned long, 7> _orientation_haptic_ms;
    bool _display_dimmed;
    DisplayDimReason _display_dim_reason;
    unsigned long _display_last_activity_ms;
    unsigned long _light_sleep_interval_ms;
    unsigned long _light_sleep_armed_ms;
    unsigned long _sleep_pending_ms;
    bool _sleep_wifi_stopped;
    unsigned long _button_wake_lockout_ms;
    bool _lift_haptic_armed;
    std::function<bool()> _timer_active_provider;
    std::function<bool()> _inactivity_allowed_provider;
};
