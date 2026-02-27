#include <unity.h>

#include "EventBus.h"
#include "../test_event_bus.h"

static void test_subscribe_and_publish_calls_handler() {
    EventBus bus;
    int call_count = 0;

    bus.subscribe(EventType::BUTTON_CLICK, [&call_count](const Event&) {
        call_count++;
    });

    bus.publish({EventType::BUTTON_CLICK, "click"});

    TEST_ASSERT_EQUAL_INT(1, call_count);
}

static void test_unsubscribe_stops_handler_calls() {
    EventBus bus;
    int call_count = 0;

    int sub_id = bus.subscribe(EventType::BUTTON_CLICK, [&call_count](const Event&) {
        call_count++;
    });

    bus.unsubscribe(EventType::BUTTON_CLICK, sub_id);
    bus.publish({EventType::BUTTON_CLICK, "click"});

    TEST_ASSERT_EQUAL_INT(0, call_count);
}

static void test_publish_uses_snapshot_when_handlers_mutate_subscriptions() {
    EventBus bus;
    int first_calls = 0;
    int second_calls = 0;
    int second_id = 0;

    bus.subscribe(EventType::BUTTON_CLICK, [&](const Event&) {
        first_calls++;
        if (second_id > 0) {
            bus.unsubscribe(EventType::BUTTON_CLICK, second_id);
        }
    });

    second_id = bus.subscribe(EventType::BUTTON_CLICK, [&](const Event&) {
        second_calls++;
    });

    bus.publish({EventType::BUTTON_CLICK, "first"});
    bus.publish({EventType::BUTTON_CLICK, "second"});

    TEST_ASSERT_EQUAL_INT(2, first_calls);
    TEST_ASSERT_EQUAL_INT(1, second_calls);
}

static void test_event_type_isolation() {
    EventBus bus;
    int click_calls = 0;
    int long_press_calls = 0;

    bus.subscribe(EventType::BUTTON_CLICK, [&](const Event&) {
        click_calls++;
    });

    bus.subscribe(EventType::BUTTON_LONG_PRESS, [&](const Event&) {
        long_press_calls++;
    });

    bus.publish({EventType::BUTTON_CLICK, "click"});

    TEST_ASSERT_EQUAL_INT(1, click_calls);
    TEST_ASSERT_EQUAL_INT(0, long_press_calls);
}

int run_event_bus_tests(void) {
    int test_count = 0;

    RUN_TEST(test_subscribe_and_publish_calls_handler);
    test_count++;
    RUN_TEST(test_unsubscribe_stops_handler_calls);
    test_count++;
    RUN_TEST(test_publish_uses_snapshot_when_handlers_mutate_subscriptions);
    test_count++;
    RUN_TEST(test_event_type_isolation);
    test_count++;

    return test_count;
}
