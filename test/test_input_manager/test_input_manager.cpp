#include <unity.h>

#include "EventBus.h"
#include "InputManager.h"
#include "SystemState.h"
#include "../test_input_manager.h"

namespace {
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
        nullptr,
        nullptr,
        nullptr,
        state_manager,
        nullptr,
        nullptr,
        nullptr,
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
}

static void test_button_click_sets_connecting_flow_when_disconnected() {
    EventBus event_bus;
    SystemStateManager state_manager;

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

    event_bus.publish({EventType::BUTTON_CLICK, "test"});

    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_CONNECTING, state_manager.getState());
    TEST_ASSERT_TRUE(pending_listening_start);
    TEST_ASSERT_TRUE(pending_connect_ui_update);
    TEST_ASSERT_TRUE(protocol_connected);

    delete input_manager;
}

static void test_button_long_press_sets_shutdown_pending() {
    EventBus event_bus;
    SystemStateManager state_manager;

    ProtocolType* protocol = nullptr;
    bool config_checked = true;
    bool protocol_connected = false;
    bool protocol_ready = false;
    bool pending_listening_start = false;
    bool shutdown_pending = false;
    unsigned long shutdown_ready_ms = 123;
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

    event_bus.publish({EventType::BUTTON_LONG_PRESS, "test"});

    TEST_ASSERT_TRUE(shutdown_pending);
    TEST_ASSERT_EQUAL_UINT32(0, shutdown_ready_ms);

    delete input_manager;
}

int run_input_manager_tests(void) {
    int test_count = 0;

    RUN_TEST(test_button_click_sets_connecting_flow_when_disconnected);
    test_count++;
    RUN_TEST(test_button_long_press_sets_shutdown_pending);
    test_count++;

    return test_count;
}
