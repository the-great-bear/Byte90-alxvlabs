#include <unity.h>

#include "EventBus.h"
#include "InputManager.h"
#include "SystemState.h"
#include "test_button_state.h"

namespace {
struct EventCounter {
    int count = 0;
    Event last_event{EventType::COUNT, ""};
};

InputManager* createInputManager(
    EventBus* event_bus,
    SystemStateManager* state_manager,
    ProtocolType*& protocol,
    bool& config_checked,
    bool& protocol_connected,
    bool& protocol_ready,
    bool& pending_listening_start,
    bool& shutdown_pending,
    unsigned long& shutdown_ready_ms,
    unsigned long& connecting_sound_last_ms,
    bool& connecting_sound_active,
    bool& pending_connect_ui_update)
{
    return new InputManager(
        event_bus,
        nullptr,              // audio
        nullptr,              // ui
        nullptr,              // haptics
        state_manager,
        nullptr,              // wifi
        nullptr,              // power manager
        nullptr,              // adxl manager
        protocol,
        config_checked,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        shutdown_pending,
        shutdown_ready_ms,
        connecting_sound_last_ms,
        connecting_sound_active,
        pending_connect_ui_update);
}
}  // namespace

static void test_button_click_speaking_publishes_abort() {
    EventBus event_bus;
    SystemStateManager state_manager;
    state_manager.setState(SYSTEM_STATE_SPEAKING);

    ProtocolType* protocol = nullptr;
    bool config_checked = true;
    bool protocol_connected = false;
    bool protocol_ready = false;
    bool pending_listening_start = false;
    bool shutdown_pending = false;
    unsigned long shutdown_ready_ms = 0;
    unsigned long connecting_sound_last_ms = 0;
    bool connecting_sound_active = false;
    bool pending_connect_ui_update = false;

    InputManager* input_manager = createInputManager(
        &event_bus,
        &state_manager,
        protocol,
        config_checked,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        shutdown_pending,
        shutdown_ready_ms,
        connecting_sound_last_ms,
        connecting_sound_active,
        pending_connect_ui_update);

    EventCounter abort_counter;
    event_bus.subscribe(EventType::ABORT_RESPONSE, [&abort_counter](const Event& event) {
        abort_counter.count++;
        abort_counter.last_event = event;
    });

    event_bus.publish({EventType::BUTTON_CLICK, "test"});

    TEST_ASSERT_EQUAL_INT(1, abort_counter.count);

    delete input_manager;
}

static void test_button_click_config_not_ready_no_connect() {
    EventBus event_bus;
    SystemStateManager state_manager;

    ProtocolType* protocol = nullptr;
    bool config_checked = false;
    bool protocol_connected = false;
    bool protocol_ready = false;
    bool pending_listening_start = false;
    bool shutdown_pending = false;
    unsigned long shutdown_ready_ms = 0;
    unsigned long connecting_sound_last_ms = 0;
    bool connecting_sound_active = false;
    bool pending_connect_ui_update = false;

    InputManager* input_manager = createInputManager(
        &event_bus,
        &state_manager,
        protocol,
        config_checked,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        shutdown_pending,
        shutdown_ready_ms,
        connecting_sound_last_ms,
        connecting_sound_active,
        pending_connect_ui_update);

    EventCounter connect_counter;
    EventCounter start_listening_counter;

    event_bus.subscribe(EventType::CONNECT_PROTOCOL, [&connect_counter](const Event& event) {
        connect_counter.count++;
        connect_counter.last_event = event;
    });
    event_bus.subscribe(EventType::START_LISTENING, [&start_listening_counter](const Event& event) {
        start_listening_counter.count++;
        start_listening_counter.last_event = event;
    });

    event_bus.publish({EventType::BUTTON_CLICK, "test"});

    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, state_manager.getState());
    TEST_ASSERT_FALSE(pending_listening_start);
    TEST_ASSERT_EQUAL_INT(0, connect_counter.count);
    TEST_ASSERT_EQUAL_INT(0, start_listening_counter.count);

    delete input_manager;
}

static void test_system_state_callback_fires_on_change() {
    SystemStateManager state_manager;
    int callback_count = 0;
    SystemState last_prev = SYSTEM_STATE_UNKNOWN;
    SystemState last_current = SYSTEM_STATE_UNKNOWN;

    state_manager.registerStateChangeCallback(
        [&callback_count, &last_prev, &last_current](SystemState prev, SystemState current) {
            callback_count++;
            last_prev = prev;
            last_current = current;
        });

    state_manager.setState(SYSTEM_STATE_LISTENING);

    TEST_ASSERT_EQUAL_INT(1, callback_count);
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, last_prev);
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_LISTENING, last_current);
}

static void test_system_state_no_callback_on_same_state() {
    SystemStateManager state_manager;
    int callback_count = 0;

    state_manager.registerStateChangeCallback(
        [&callback_count](SystemState, SystemState) {
            callback_count++;
        });

    state_manager.setState(SYSTEM_STATE_IDLE);

    TEST_ASSERT_EQUAL_INT(0, callback_count);
}

int run_button_state_tests(void) {
    int testCount = 0;

    RUN_TEST(test_button_click_speaking_publishes_abort);
    testCount++;
    RUN_TEST(test_button_click_config_not_ready_no_connect);
    testCount++;
    RUN_TEST(test_system_state_callback_fires_on_change);
    testCount++;
    RUN_TEST(test_system_state_no_callback_on_same_state);
    testCount++;

    return testCount;
}
