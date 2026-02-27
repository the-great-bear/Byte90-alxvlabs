/**
 * DigitalClock.cpp
 *
 * Implementation for DigitalClock.
 */

#include "DigitalClock.h"

#include "ArduinoSSD1351.h"

#include <Fonts/FreeSansBold9pt7b.h>
#include <esp_log.h>
#include <string.h>
#include <time.h>

namespace {
static const char* log_tag = "DigitalClock";
static const uint16_t color_white = 0xFFFF;
static const uint16_t color_cyan = 0x07FF;
static const uint16_t color_black = 0x0000;
static const uint16_t color_red = 0xF800;
static const uint16_t color_yellow = 0xFFE0;
static const int clock_top = 16;
static const int time_y = 74;
static const int ampm_x = 101;
static const int ampm_y = 98;
static const int day_x = 4;
static const int day_y = 105;
static const int date_box_height = 13;
static const int date_text_y = 118;
static const time_t min_valid_time = 1600000000;
}

DigitalClock::DigitalClock(ArduinoSSD1351* display)
    : _display(display)
{
    _last_time[0] = '\0';
    _last_ampm[0] = '\0';
    _last_date[0] = '\0';
    _last_day[0] = '\0';
    _initialized = false;
}

void DigitalClock::reset() {
    _last_time[0] = '\0';
    _last_ampm[0] = '\0';
    _last_date[0] = '\0';
    _last_day[0] = '\0';
    _initialized = false;
}

void DigitalClock::buildStrings(char* time_buf, size_t time_len,
                                char* date_buf, size_t date_len,
                                char* day_buf, size_t day_len,
                                char* ampm_buf, size_t ampm_len,
                                bool* time_valid) const {
    time_t now = time(nullptr);
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
        if (time_len > 0) {
            time_buf[0] = '\0';
        }
        if (date_len > 0) {
            date_buf[0] = '\0';
        }
        if (day_len > 0) {
            day_buf[0] = '\0';
        }
        if (ampm_len > 0) {
            ampm_buf[0] = '\0';
        }
        if (time_valid) {
            *time_valid = false;
        }
        return;
    }

    bool valid = (now >= min_valid_time);
    if (time_valid) {
        *time_valid = valid;
    }

    int display_hour = timeinfo.tm_hour;
    bool is_pm = false;
    if (display_hour == 0) {
        display_hour = 12;
    } else if (display_hour > 12) {
        display_hour -= 12;
        is_pm = true;
    } else if (display_hour == 12) {
        is_pm = true;
    }

    snprintf(time_buf, time_len, "%2d:%02d", display_hour, timeinfo.tm_min);
    snprintf(ampm_buf, ampm_len, "%s", is_pm ? "PM" : "AM");

    static const char* short_months[] = {
        "XXX", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };

    if (valid) {
        snprintf(date_buf, date_len, "%s %d, %d",
                 short_months[timeinfo.tm_mon + 1],
                 timeinfo.tm_mday,
                 timeinfo.tm_year + 1900);
        strftime(day_buf, day_len, "%a", &timeinfo);
        for (int i = 0; day_buf[i]; ++i) {
            if (day_buf[i] >= 'a' && day_buf[i] <= 'z') {
                day_buf[i] = (char)(day_buf[i] - 'a' + 'A');
            }
        }
    } else {
        snprintf(date_buf, date_len, "%s", "TIME NOT SET");
        snprintf(day_buf, day_len, "%s", "INVALID");
    }
}

void DigitalClock::draw() {
    if (!_display) {
        return;
    }

    char time_buf[16];
    char date_buf[16];
    char day_buf[16];
    char ampm_buf[8];
    bool time_valid = false;
    buildStrings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf),
                 day_buf, sizeof(day_buf), ampm_buf, sizeof(ampm_buf), &time_valid);

    if (time_buf[0] == '\0') {
        ESP_LOGW(log_tag, "Local time unavailable");
        return;
    }

    auto* gfx = _display->getAdafruitDisplay();
    int16_t x1, y1;
    uint16_t w, h;
    int16_t width = gfx->width();
    int16_t height = gfx->height();

    if (!_initialized) {
        gfx->fillRect(0, clock_top, width, height - clock_top, color_black);
        _last_time[0] = '\0';
        _last_ampm[0] = '\0';
        _last_date[0] = '\0';
        _last_day[0] = '\0';
        _initialized = true;
    }

    uint16_t primary_color = time_valid ? color_white : color_red;
    uint16_t accent_color = time_valid ? color_cyan : color_yellow;

    if (strcmp(time_buf, _last_time) != 0) {
        gfx->setFont(&FreeSansBold9pt7b);
        gfx->setTextSize(3);
        if (strlen(_last_time) > 0) {
            gfx->getTextBounds(_last_time, 0, 0, &x1, &y1, &w, &h);
            int old_time_x = (width - (int16_t)w) / 2 - 6;
            gfx->fillRect(old_time_x - 5, time_y + y1 - 3, w + 15, h + 6, color_black);
        }
        gfx->setTextWrap(false);
        gfx->setTextColor(primary_color);
        gfx->getTextBounds(time_buf, 0, 0, &x1, &y1, &w, &h);
        int time_x = (width - (int16_t)w) / 2 - 6;
        gfx->setCursor(time_x, time_y);
        gfx->print(time_buf);
        strncpy(_last_time, time_buf, sizeof(_last_time));
        _last_time[sizeof(_last_time) - 1] = '\0';
    }

    if (strcmp(ampm_buf, _last_ampm) != 0) {
        gfx->setFont();
        gfx->setTextSize(2);
        int16_t am_x1, am_y1, pm_x1, pm_y1;
        uint16_t am_w, am_h, pm_w, pm_h;
        gfx->getTextBounds("AM", 0, 0, &am_x1, &am_y1, &am_w, &am_h);
        gfx->getTextBounds("PM", 0, 0, &pm_x1, &pm_y1, &pm_w, &pm_h);
        uint16_t max_w = (am_w > pm_w) ? am_w : pm_w;
        uint16_t max_h = (am_h > pm_h) ? am_h : pm_h;
        gfx->fillRect(ampm_x - 2, ampm_y - max_h - 2, max_w + 6, max_h + 6, color_black);
        gfx->setTextColor(accent_color);
        gfx->setCursor(ampm_x, ampm_y);
        gfx->print(ampm_buf);
        strncpy(_last_ampm, ampm_buf, sizeof(_last_ampm));
        _last_ampm[sizeof(_last_ampm) - 1] = '\0';
    }

    if (strcmp(day_buf, _last_day) != 0) {
        gfx->setFont();
        gfx->setTextSize(1);
        gfx->fillRect(2, 100, 80, 12, color_black);
        gfx->setTextColor(accent_color);
        gfx->setCursor(day_x, day_y);
        gfx->print(day_buf);
        strncpy(_last_day, day_buf, sizeof(_last_day));
        _last_day[sizeof(_last_day) - 1] = '\0';
    }

    bool date_changed = (strcmp(date_buf, _last_date) != 0);
    bool force_invalid = (!time_valid && strcmp(date_buf, "TIME NOT SET") == 0);
    if (date_changed || force_invalid) {
        int box_y = height - date_box_height;
        gfx->fillRect(0, box_y, width, date_box_height, color_black);
        gfx->fillRect(0, box_y, width, date_box_height, accent_color);
        gfx->setFont();
        gfx->setTextSize(1);
        gfx->setTextColor(color_black);
        gfx->setCursor(4, date_text_y);
        gfx->print(date_buf);
        strncpy(_last_date, date_buf, sizeof(_last_date));
        _last_date[sizeof(_last_date) - 1] = '\0';
    }
}
