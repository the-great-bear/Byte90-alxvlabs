#include <unity.h>

#include "SystemState.h"
#include "../test_system_state.h"

static void test_default_state_is_idle() {
    SystemStateManager state_manager;

    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, state_manager.getState());
}

static void test_state_change_callback_reports_previous_and_current() {
    SystemStateManager state_manager;
    int callback_count = 0;
    SystemState previous = SYSTEM_STATE_UNKNOWN;
    SystemState current = SYSTEM_STATE_UNKNOWN;

    state_manager.registerStateChangeCallback(
        [&](SystemState prev, SystemState now) {
            callback_count++;
            previous = prev;
            current = now;
        });

    state_manager.setState(SYSTEM_STATE_LISTENING);

    TEST_ASSERT_EQUAL_INT(1, callback_count);
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, previous);
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_LISTENING, current);
}

static void test_same_state_does_not_invoke_callback() {
    SystemStateManager state_manager;
    int callback_count = 0;

    state_manager.registerStateChangeCallback(
        [&](SystemState, SystemState) {
            callback_count++;
        });

    state_manager.setState(SYSTEM_STATE_IDLE);

    TEST_ASSERT_EQUAL_INT(0, callback_count);
}

static void test_initialize_wifi_without_manager_fails() {
    SystemStateManager state_manager;

    TEST_ASSERT_FALSE(state_manager.initializeWiFi(nullptr));
}

static void test_state_string_for_known_values() {
    SystemStateManager state_manager;

    TEST_ASSERT_EQUAL_STRING("Idle", state_manager.getStateString(SYSTEM_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("Listening", state_manager.getStateString(SYSTEM_STATE_LISTENING));
}

int run_system_state_tests(void) {
    int test_count = 0;

    RUN_TEST(test_default_state_is_idle);
    test_count++;
    RUN_TEST(test_state_change_callback_reports_previous_and_current);
    test_count++;
    RUN_TEST(test_same_state_does_not_invoke_callback);
    test_count++;
    RUN_TEST(test_initialize_wifi_without_manager_fails);
    test_count++;
    RUN_TEST(test_state_string_for_known_values);
    test_count++;

    return test_count;
}
