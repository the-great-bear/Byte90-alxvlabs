/**
 * DigitalClockController.h
 *
 * Clock control interface for UI implementations.
 */

#pragma once

#include <Arduino.h>

class DigitalClockController {
public:
    virtual ~DigitalClockController() = default;
    virtual bool showClock(const String& timezone_name) = 0;
    virtual void clearClock() = 0;
    virtual bool isShowingClock() const = 0;
};
