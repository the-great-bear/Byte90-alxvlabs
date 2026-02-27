/**
 * DigitalClock.h
 *
 * Declarations for DigitalClock.
 */

#pragma once

#include <Arduino.h>

class ArduinoSSD1351;

/**
 * DigitalClock - Simple digital clock renderer.
 */
class DigitalClock {
public:
    explicit DigitalClock(ArduinoSSD1351* display);

    void reset();
    void draw();

private:
    void buildStrings(char* time_buf, size_t time_len,
                      char* date_buf, size_t date_len,
                      char* day_buf, size_t day_len,
                      char* ampm_buf, size_t ampm_len,
                      bool* time_valid) const;

    ArduinoSSD1351* _display;
    char _last_time[16];
    char _last_ampm[8];
    char _last_date[16];
    char _last_day[16];
    bool _initialized;
};
