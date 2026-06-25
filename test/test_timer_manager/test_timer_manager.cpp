#include <unity.h>

#include "TimerManager.h"
#include "../test_timer_manager.h"

static void test_start_zero_seconds_fails() {
    TimerManager timer;

    uint8_t id = timer.start(0);

    TEST_ASSERT_EQUAL_UINT8(0, id);
    TEST_ASSERT_FALSE(timer.isRunning());
}

static void test_start_returns_nonzero_id() {
    TimerManager timer;

    uint8_t id = timer.start(60);
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(timer.isRunning());
    TEST_ASSERT_TRUE(timer.cancel(id));
}

static void test_start_with_label() {
    TimerManager timer;

    uint8_t id = timer.start(30, "pasta", TimerManager::DisplayFormat::Seconds);
    TEST_ASSERT_NOT_EQUAL(0, id);

    const TimerManager::TimerEntry* e = timer.getEntry(id);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("pasta", e->label);
    TEST_ASSERT_EQUAL_UINT32(30, e->duration_seconds);

    TEST_ASSERT_TRUE(timer.cancel(id));
}

static void test_multiple_concurrent_timers() {
    TimerManager timer;

    uint8_t id1 = timer.start(60, "first",  TimerManager::DisplayFormat::Seconds);
    uint8_t id2 = timer.start(120, "second", TimerManager::DisplayFormat::Minutes);

    TEST_ASSERT_NOT_EQUAL(0, id1);
    TEST_ASSERT_NOT_EQUAL(0, id2);
    TEST_ASSERT_NOT_EQUAL(id1, id2);
    TEST_ASSERT_TRUE(timer.isRunning());
    TEST_ASSERT_TRUE(timer.isRunning(id1));
    TEST_ASSERT_TRUE(timer.isRunning(id2));

    auto active = timer.listActive();
    TEST_ASSERT_EQUAL(2, active.size());

    TEST_ASSERT_TRUE(timer.cancel(id1));
    TEST_ASSERT_TRUE(timer.cancel(id2));
}

static void test_cancel_most_recent_when_id_zero() {
    TimerManager timer;

    uint8_t id1 = timer.start(60);
    uint8_t id2 = timer.start(120);
    (void)id1;

    TEST_ASSERT_NOT_EQUAL(0, id2);

    // cancel(0) targets most-recent (id2)
    TEST_ASSERT_TRUE(timer.cancel(0));
    TEST_ASSERT_FALSE(timer.isRunning(id2));
    TEST_ASSERT_TRUE(timer.isRunning(id1));

    TEST_ASSERT_TRUE(timer.cancel(id1));
}

static void test_cancel_by_id() {
    TimerManager timer;

    uint8_t id1 = timer.start(60);
    uint8_t id2 = timer.start(120);

    TEST_ASSERT_TRUE(timer.cancel(id1));
    TEST_ASSERT_FALSE(timer.isRunning(id1));
    TEST_ASSERT_TRUE(timer.isRunning(id2));

    TEST_ASSERT_TRUE(timer.cancel(id2));
}

static void test_repeat_uses_last_duration_and_label() {
    TimerManager timer;

    uint8_t id = timer.start(30, "coffee", TimerManager::DisplayFormat::Minutes);
    TEST_ASSERT_TRUE(timer.cancel(id));

    TEST_ASSERT_EQUAL_UINT32(30, timer.lastDurationSeconds());

    uint8_t id2 = timer.repeatTimer(0);
    TEST_ASSERT_NOT_EQUAL(0, id2);
    TEST_ASSERT_TRUE(timer.isRunning(id2));

    const TimerManager::TimerEntry* e = timer.getEntry(id2);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("coffee", e->label);

    TEST_ASSERT_TRUE(timer.cancel(id2));
}

static void test_max_timers_enforced() {
    TimerManager timer;

    uint8_t ids[TIMER_MAX_ENTRIES];
    for (uint8_t i = 0; i < TIMER_MAX_ENTRIES; i++) {
        ids[i] = timer.start(3600);
        TEST_ASSERT_NOT_EQUAL(0, ids[i]);
    }

    uint8_t overflow = timer.start(60);
    TEST_ASSERT_EQUAL_UINT8(0, overflow);

    for (uint8_t i = 0; i < TIMER_MAX_ENTRIES; i++) {
        timer.cancel(ids[i]);
    }
}

static void test_soonest_expiring_reflected_in_remaining() {
    TimerManager timer;

    uint8_t id1 = timer.start(10);
    uint8_t id2 = timer.start(3600);
    (void)id2;

    // remainingSeconds() should reflect the soonest timer (<=10 s)
    TEST_ASSERT_TRUE(timer.remainingSeconds() <= 10);

    timer.cancel(id1);
    timer.cancel(id2);
}

static void test_expiry_callback_fires_with_id_and_label() {
    TimerManager timer;
    uint8_t fired_id = 0;
    String fired_label;

    timer.setExpiredCallback([&](uint8_t id, const char* label) {
        fired_id    = id;
        fired_label = label;
    });

    uint8_t id = timer.start(1, "test", TimerManager::DisplayFormat::Seconds);
    delay(1200);
    timer.update();
    timer.update();

    TEST_ASSERT_FALSE(timer.isRunning(id));
    TEST_ASSERT_EQUAL_UINT8(id, fired_id);
    TEST_ASSERT_EQUAL_STRING("test", fired_label.c_str());
}

int run_timer_manager_tests(void) {
    int n = 0;

    RUN_TEST(test_start_zero_seconds_fails);               n++;
    RUN_TEST(test_start_returns_nonzero_id);               n++;
    RUN_TEST(test_start_with_label);                       n++;
    RUN_TEST(test_multiple_concurrent_timers);             n++;
    RUN_TEST(test_cancel_most_recent_when_id_zero);        n++;
    RUN_TEST(test_cancel_by_id);                           n++;
    RUN_TEST(test_repeat_uses_last_duration_and_label);    n++;
    RUN_TEST(test_max_timers_enforced);                    n++;
    RUN_TEST(test_soonest_expiring_reflected_in_remaining); n++;
    RUN_TEST(test_expiry_callback_fires_with_id_and_label); n++;

    return n;
}
