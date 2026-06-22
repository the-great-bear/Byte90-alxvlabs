#include "ClockSync.h"

#include "DeviceConfig.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_log.h>
#include <string.h>
#include <sys/time.h>

namespace {
static const char* log_tag = "ClockSync";
static const char* prefs_namespace = "system";
static const char* timezone_key = "timezone";
static const time_t min_valid_time = 1600000000;

struct TimezoneAlias {
    const char* alias;
    const char* canonical;
};

static const ClockSync::TimezoneInfo available_timezones[] = {
    {"North_America_Eastern", "EST5EDT,M3.2.0,M11.1.0",
     "Eastern Time (EST/EDT) UTC-5/-4"},
    {"North_America_Central", "CST6CDT,M3.2.0,M11.1.0",
     "Central Time (CST/CDT) UTC-6/-5"},
    {"North_America_Mountain", "MST7MDT,M3.2.0,M11.1.0",
     "Mountain Time (MST/MDT) UTC-7/-6"},
    {"North_America_Pacific", "PST8PDT,M3.2.0,M11.1.0",
     "Pacific Time (PST/PDT) UTC-8/-7"},
    {"North_America_Alaska", "AKST9AKDT,M3.2.0,M11.1.0",
     "Alaska Time (AKST/AKDT) UTC-9/-8"},
    {"North_America_Hawaii", "HST10", "Hawaii Time (HST) UTC-10"},

    {"UTC", "UTC0", "Coordinated Universal Time UTC+0"},
    {"UK", "GMT0BST,M3.5.0,M10.5.0", "United Kingdom (GMT/BST) UTC+0/+1"},
    {"Central_Europe", "CET-1CEST,M3.5.0,M10.5.0",
     "Central Europe (CET/CEST) UTC+1/+2"},
    {"Eastern_Europe", "EET-2EEST,M3.5.0,M10.5.0",
     "Eastern Europe (EET/EEST) UTC+2/+3"},
    {"Japan", "JST-9", "Japan Standard Time (JST) UTC+9"},
    {"China", "CST-8", "China Standard Time (CST) UTC+8"},
    {"Australia_Eastern", "AEST-10AEDT,M10.1.0,M4.1.0",
     "Australia Eastern (AEST/AEDT) UTC+10/+11"},
    {"Australia_Central", "ACST-9:30ACDT,M10.1.0,M4.1.0",
     "Australia Central (ACST/ACDT) UTC+9:30/+10:30"},
    {"Australia_Western", "AWST-8", "Australia Western (AWST) UTC+8"},
};

static const size_t timezone_count =
    sizeof(available_timezones) / sizeof(available_timezones[0]);

static const TimezoneAlias timezone_aliases[] = {
    {"America/New_York", "North_America_Eastern"},
    {"America/Detroit", "North_America_Eastern"},
    {"America/Toronto", "North_America_Eastern"},
    {"US/Eastern", "North_America_Eastern"},
    {"EST5EDT", "North_America_Eastern"},

    {"America/Chicago", "North_America_Central"},
    {"America/Winnipeg", "North_America_Central"},
    {"US/Central", "North_America_Central"},
    {"CST6CDT", "North_America_Central"},

    {"America/Denver", "North_America_Mountain"},
    {"America/Edmonton", "North_America_Mountain"},
    {"US/Mountain", "North_America_Mountain"},
    {"MST7MDT", "North_America_Mountain"},

    {"America/Los_Angeles", "North_America_Pacific"},
    {"America/Vancouver", "North_America_Pacific"},
    {"US/Pacific", "North_America_Pacific"},
    {"PST8PDT", "North_America_Pacific"},

    {"America/Anchorage", "North_America_Alaska"},
    {"US/Alaska", "North_America_Alaska"},
    {"AKST9AKDT", "North_America_Alaska"},

    {"Pacific/Honolulu", "North_America_Hawaii"},
    {"US/Hawaii", "North_America_Hawaii"},
    {"HST10", "North_America_Hawaii"},

    {"UTC", "UTC"},
    {"Etc/UTC", "UTC"},
    {"Etc/GMT", "UTC"},

    {"Europe/London", "UK"},
    {"Europe/Berlin", "Central_Europe"},
    {"Europe/Paris", "Central_Europe"},
    {"Europe/Warsaw", "Central_Europe"},
    {"Europe/Athens", "Eastern_Europe"},
    {"Europe/Bucharest", "Eastern_Europe"},
    {"Europe/Moscow", "Eastern_Europe"},

    {"Asia/Tokyo", "Japan"},
    {"Asia/Shanghai", "China"},
    {"Asia/Hong_Kong", "China"},
    {"Asia/Singapore", "China"},

    {"Australia/Sydney", "Australia_Eastern"},
    {"Australia/Melbourne", "Australia_Eastern"},
    {"Australia/Brisbane", "Australia_Eastern"},
    {"Australia/Adelaide", "Australia_Central"},
    {"Australia/Darwin", "Australia_Central"},
    {"Australia/Perth", "Australia_Western"},
};

static const size_t timezone_alias_count =
    sizeof(timezone_aliases) / sizeof(timezone_aliases[0]);

const char* resolveTimezoneAlias(const char* timezone_name) {
    if (timezone_name == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < timezone_alias_count; ++i) {
        if (strcmp(timezone_aliases[i].alias, timezone_name) == 0) {
            return timezone_aliases[i].canonical;
        }
    }

    return timezone_name;
}

const char* pickDefaultServer(const char* timezone) {
    if (timezone == nullptr || timezone[0] == '\0') {
        return NTP_SERVER_DEFAULT;
    }

    if (strstr(timezone, "CST-8") != nullptr) {
        return NTP_SERVER_CHINA;
    }

    return NTP_SERVER_DEFAULT;
}

void applySavedTimezoneIfPresent() {
    Preferences prefs;
    if (!prefs.begin(prefs_namespace, true)) {
        return;
    }
    String tz = prefs.getString(timezone_key, "");
    prefs.end();
    if (!tz.isEmpty()) {
        setenv("TZ", tz.c_str(), 1);
        tzset();
    }
}
}  // namespace

const ClockSync::TimezoneInfo* ClockSync::getAvailableTimezones(size_t& count) {
    count = timezone_count;
    return available_timezones;
}

const char* ClockSync::resolveNtpServer(const char* timezone, const char* ntp_server) {
    if (ntp_server != nullptr && ntp_server[0] != '\0') {
        return ntp_server;
    }

    return pickDefaultServer(timezone);
}

bool ClockSync::setTimezone(const char* timezone) {
    if (timezone == nullptr || timezone[0] == '\0') {
        return false;
    }

    setenv("TZ", timezone, 1);
    tzset();

    Preferences prefs;
    if (!prefs.begin(prefs_namespace, false)) {
        return false;
    }
    bool stored = prefs.putString(timezone_key, timezone) > 0;
    prefs.end();

    return stored;
}

bool ClockSync::setTimezoneByName(const char* timezone_name) {
    if (timezone_name == nullptr || timezone_name[0] == '\0') {
        return false;
    }

    const char* resolved_name = resolveTimezoneAlias(timezone_name);
    for (size_t i = 0; i < timezone_count; ++i) {
        if (strcmp(available_timezones[i].name, resolved_name) == 0) {
            return setTimezone(available_timezones[i].tz_string);
        }
    }

    ESP_LOGE(log_tag, "Timezone '%s' not found", timezone_name);
    return false;
}

String ClockSync::getTimezone() {
    Preferences prefs;
    if (!prefs.begin(prefs_namespace, true)) {
        return String();
    }

    String tz = prefs.getString(timezone_key, "");
    prefs.end();
    return tz;
}

bool ClockSync::syncNow(const char* ntp_server, const char* timezone, uint32_t timeout_ms) {
    String tz_value;
    const char* tz_ptr = timezone;
    if (timezone != nullptr && timezone[0] != '\0') {
        if (!setTimezone(timezone)) {
            ESP_LOGW(log_tag, "Failed to set timezone: %s", timezone);
        }
    } else {
        applySavedTimezoneIfPresent();
    }

    tz_value = getTimezone();
    if (!tz_value.isEmpty()) {
        tz_ptr = tz_value.c_str();
    }

    const char* server = resolveNtpServer(tz_ptr, ntp_server);
    ESP_LOGI(log_tag, "Syncing time with NTP server: %s", server);
    if (tz_ptr && tz_ptr[0] != '\0') {
        configTzTime(tz_ptr, server);
    } else {
        configTime(0, 0, server);
    }

    uint32_t start_ms = millis();
    time_t before = time(nullptr);
    time_t now = before;
    while ((millis() - start_ms) < timeout_ms) {
        delay(100);
        now = time(nullptr);
        if (now >= min_valid_time) {
            if (before < min_valid_time || now != before) {
                break;
            }
        }
    }

    bool synced = now >= min_valid_time && (before < min_valid_time || now != before);

    if (!synced) {
        // Some routers block outbound UDP 123 to external IPs (while running their
        // own NTP service) — a side-effect of the DNS override to 8.8.8.8 that was
        // added to work around captive DNS interception. Try the DHCP gateway as a
        // fallback; it answers on the local network and responds in < 100 ms.
        String gateway = WiFi.gatewayIP().toString();
        if (!gateway.isEmpty() && gateway != "0.0.0.0") {
            ESP_LOGI(log_tag, "NTP fallback: trying gateway %s", gateway.c_str());
            if (tz_ptr && tz_ptr[0] != '\0') {
                configTzTime(tz_ptr, gateway.c_str());
            } else {
                configTime(0, 0, gateway.c_str());
            }

            uint32_t fb_start = millis();
            time_t fb_before = time(nullptr);
            now = fb_before;
            while ((millis() - fb_start) < 2000) {
                delay(100);
                now = time(nullptr);
                if (now >= min_valid_time) {
                    if (fb_before < min_valid_time || now != fb_before) {
                        break;
                    }
                }
            }
            synced = now >= min_valid_time && (fb_before < min_valid_time || now != fb_before);
        }
    }

    if (synced) {
        struct tm local_tm;
        struct tm utc_tm;
        localtime_r(&now, &local_tm);
        gmtime_r(&now, &utc_tm);

        char local_buf[32];
        char utc_buf[32];
        strftime(local_buf, sizeof(local_buf), "%Y-%m-%dT%H:%M:%S", &local_tm);
        strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);

        String tz = getTimezone();
        ESP_LOGI(log_tag, "Time sync OK tz=%s local=%s utc=%s",
                 tz.isEmpty() ? "(unset)" : tz.c_str(),
                 local_buf,
                 utc_buf);
    } else {
        ESP_LOGW(log_tag, "Time sync timeout (tz=%s server=%s)",
                 (timezone != nullptr && timezone[0] != '\0') ? timezone : "(unset)",
                 resolveNtpServer(timezone, ntp_server));
    }

    return synced;
}

bool ClockSync::isTimeValid() {
    return time(nullptr) >= min_valid_time;
}

long long ClockSync::epochMs() {
    time_t now = time(nullptr);
    if (now < min_valid_time) {
        return 0;
    }
    return static_cast<long long>(now) * 1000LL;
}

String ClockSync::formatLocalIso8601() {
    time_t now = time(nullptr);
    if (now < min_valid_time) {
        return String();
    }

    applySavedTimezoneIfPresent();
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_tm);
    return String(buffer);
}

String ClockSync::formatUtcIso8601() {
    time_t now = time(nullptr);
    if (now < min_valid_time) {
        return String();
    }

    struct tm utc_tm;
    gmtime_r(&now, &utc_tm);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    return String(buffer);
}
