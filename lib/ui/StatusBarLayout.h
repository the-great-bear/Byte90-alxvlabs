/**
 * StatusBarLayout.h
 *
 * Shared layout constants for status bar rendering and GIF exclusion.
 */

#pragma once

// Display dimensions
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 128

// Activity dots layout (top left corner)
#define ACTIVITY_DOTS_X             2
#define ACTIVITY_DOTS_Y             5
#define ACTIVITY_DOT_WIDTH          3
#define ACTIVITY_DOT_GAP            2
#define ACTIVITY_DOT_COUNT          4
#define ACTIVITY_DOT_SPACING        (ACTIVITY_DOT_WIDTH + ACTIVITY_DOT_GAP)
#define ACTIVITY_DOTS_WIDTH         25
#define ACTIVITY_DOTS_HEIGHT        12

// Status bar layout (top right corner)
#define STATUS_BAR_Y        2
#define WIFI_CIRCLE_SIZE    6
#define WIFI_CIRCLE_X       (SCREEN_WIDTH - 2 - WIFI_CIRCLE_SIZE)
#define WIFI_CIRCLE_Y       STATUS_BAR_Y

// Battery icon layout (to the left of WiFi circle)
#define BATTERY_WIDTH               15
#define BATTERY_HEIGHT              7
#define BATTERY_ROUND_RADIUS        3
#define BATTERY_OFFSET_FROM_WIFI    3 // Gap between WiFi circle and battery icon
#define BATTERY_X                   (WIFI_CIRCLE_X - BATTERY_WIDTH - BATTERY_OFFSET_FROM_WIFI)
#define BATTERY_Y                   (WIFI_CIRCLE_Y + (WIFI_CIRCLE_SIZE - BATTERY_HEIGHT) / 2)
#define BATTERY_FILL_WIDTH          (BATTERY_WIDTH - 4)
#define BATTERY_FILL_HEIGHT         3
#define BATTERY_FILL_X_OFFSET       2
#define BATTERY_FILL_Y_OFFSET       2

// Timer layout (to the right of activity dots)
#define TIMER_TEXT_GAP              2
#define TIMER_TEXT_MAX_CHARS        8
#define TIMER_TEXT_CHAR_WIDTH       6
#define TIMER_TEXT_WIDTH            (TIMER_TEXT_MAX_CHARS * TIMER_TEXT_CHAR_WIDTH)
#define TIMER_TEXT_X                (ACTIVITY_DOTS_X + ACTIVITY_DOTS_WIDTH + TIMER_TEXT_GAP)
#define TIMER_TEXT_Y                (BATTERY_Y + (BATTERY_HEIGHT / 2) - 3)
#define TIMER_TEXT_HEIGHT           (WIFI_CIRCLE_SIZE + 4)

// Status bar bounds for GIF exclusion
#define STATUS_BAR_X                TIMER_TEXT_X
#define STATUS_BAR_WIDTH            (SCREEN_WIDTH - STATUS_BAR_X)
#define STATUS_BAR_EXCLUDE_Y        ((TIMER_TEXT_Y < STATUS_BAR_Y) ? TIMER_TEXT_Y : STATUS_BAR_Y)
#define STATUS_BAR_EXCLUDE_BOTTOM   (((TIMER_TEXT_Y + TIMER_TEXT_HEIGHT) > (STATUS_BAR_Y + WIFI_CIRCLE_SIZE)) \
                                     ? (TIMER_TEXT_Y + TIMER_TEXT_HEIGHT) \
                                     : (STATUS_BAR_Y + WIFI_CIRCLE_SIZE))
#define STATUS_BAR_EXCLUDE_HEIGHT   (STATUS_BAR_EXCLUDE_BOTTOM - STATUS_BAR_EXCLUDE_Y)
