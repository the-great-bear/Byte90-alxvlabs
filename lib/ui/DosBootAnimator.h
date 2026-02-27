/**
 * DosBootAnimator.h
 *
 * DOS-style boot animation using TypingEffect and ToneGenerator.
 */

#pragma once

#include "ArduinoSSD1351.h"
#include "TypingEffect.h"
#include <Arduino.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ToneGenerator;

/**
 * @brief DosBootAnimator.
 */
class DosBootAnimator {
public:
    DosBootAnimator();

    void begin(ArduinoSSD1351* display, SemaphoreHandle_t display_mutex);
    void setToneGenerator(ToneGenerator* tone);
    void setOnFinished(std::function<void()> callback);
    void setTintColor(uint16_t color, bool enabled);

    bool startFast();
    void runFast();
    void stop();
    bool isRunning() const { return _running; }

private:
    static void taskEntry(void* parameter);
    void runFastInternal();

    bool lockDisplay();
    void unlockDisplay();

    ArduinoSSD1351* _display;
    SemaphoreHandle_t _display_mutex;
    TypingEffect _typing;
    ToneGenerator* _tone;
    std::function<void()> _on_finished;
    bool _running;
    bool _tint_enabled;
    uint16_t _tint_color;
};
