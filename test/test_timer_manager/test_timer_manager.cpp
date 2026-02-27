#include <unity.h>

#include "TimerManager.h"
#include "../test_timer_manager.h"

static void test_start_zero_seconds_fails() {
    TimerManager timer;

    bool started = timer.start(0);

    TEST_ASSERT_FALSE(started);
    TEST_ASSERT_FALSE(timer.isRunning());
}

static void test_start_while_running_fails() {
    TimerManager timer;

    TEST_ASSERT_TRUE(timer.start(1));
    TEST_ASSERT_FALSE(timer.start(1));
    TEST_ASSERT_TRUE(timer.isRunning());

    TEST_ASSERT_TRUE(timer.cancel());
}

static void test_cancel_clears_running_state() {
    TimerManager timer;

    TEST_ASSERT_TRUE(timer.start(1));
    TEST_ASSERT_TRUE(timer.cancel());
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(0, timer.remainingSeconds());
    TEST_ASSERT_FALSE(timer.cancel());
}

static void test_repeat_uses_last_duration_and_format() {
    TimerManager timer;

    TEST_ASSERT_TRUE(timer.start(1, TimerManager::DisplayFormat::Minutes));
    TEST_ASSERT_TRUE(timer.cancel());

    TEST_ASSERT_EQUAL_UINT32(1, timer.lastDurationSeconds());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TimerManager::DisplayFormat::Minutes),
        static_cast<int>(timer.lastDisplayFormat()));

    TEST_ASSERT_TRUE(timer.repeat());
    TEST_ASSERT_TRUE(timer.isRunning());
    TEST_ASSERT_TRUE(timer.cancel());
}

static void test_expiry_callback_fires_once() {
    TimerManager timer;
    int callback_count = 0;

    timer.setExpiredCallback([&callback_count]() {
        callback_count++;
    });

    TEST_ASSERT_TRUE(timer.start(1));
    delay(1200);
    timer.update();
    timer.update();

    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_INT(1, callback_count);
}

int run_timer_manager_tests(void) {
    int test_count = 0;

    RUN_TEST(test_start_zero_seconds_fails);
    test_count++;
    RUN_TEST(test_start_while_running_fails);
    test_count++;
    RUN_TEST(test_cancel_clears_running_state);
    test_count++;
    RUN_TEST(test_repeat_uses_last_duration_and_format);
    test_count++;
    RUN_TEST(test_expiry_callback_fires_once);
    test_count++;

    return test_count;
}
