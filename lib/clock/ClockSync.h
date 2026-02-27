#pragma once

#include <Arduino.h>
#include <time.h>

class ClockSync {
public:
    struct TimezoneInfo {
        const char* name;
        const char* tz_string;
        const char* description;
    };

    static const TimezoneInfo* getAvailableTimezones(size_t& count);

    bool syncNow(const char* ntp_server = nullptr,
                 const char* timezone = nullptr,
                 uint32_t timeout_ms = 2000);

    bool setTimezone(const char* timezone);
    bool setTimezoneByName(const char* timezone_name);
    String getTimezone();

    bool isTimeValid();
    long long epochMs();
    String formatLocalIso8601();
    String formatUtcIso8601();

    const char* resolveNtpServer(const char* timezone, const char* ntp_server);
};
