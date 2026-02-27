#include <unity.h>

#include "ProtocolManager.h"
#include "SystemState.h"
#include "TenclassClient.h"
#include "../test_protocol_manager.h"

static void test_start_listening_sets_pending_when_mcp_not_ready() {
    ProtocolType* protocol = nullptr;
    ProtocolConfig config;
    SystemStateManager state_manager_instance;
    SystemStateManager* state_manager = &state_manager_instance;
    ApplicationAudio* audio = nullptr;
    McpServer* mcp_server = nullptr;
    bool protocol_connected = false;
    bool protocol_ready = false;
    bool pending_listening_start = false;

    ProtocolManager manager(
        nullptr,
        protocol,
        config,
        state_manager,
        audio,
        mcp_server,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        nullptr);

    manager.startListening();

    TEST_ASSERT_TRUE(pending_listening_start);
}

static void test_perform_disconnect_resets_flags_and_state() {
    ProtocolType* protocol = nullptr;
    ProtocolConfig config;
    SystemStateManager state_manager_instance;
    SystemStateManager* state_manager = &state_manager_instance;
    ApplicationAudio* audio = nullptr;
    McpServer* mcp_server = nullptr;
    bool protocol_connected = true;
    bool protocol_ready = true;
    bool pending_listening_start = true;

    ProtocolManager manager(
        nullptr,
        protocol,
        config,
        state_manager,
        audio,
        mcp_server,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        nullptr);

    state_manager_instance.setState(SYSTEM_STATE_CONNECTING);
    manager.performDisconnect(false);

    TEST_ASSERT_FALSE(protocol_connected);
    TEST_ASSERT_FALSE(protocol_ready);
    TEST_ASSERT_FALSE(pending_listening_start);
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, state_manager_instance.getState());
}

static void test_get_ui_protocol_state_null_when_protocol_missing() {
    ProtocolType* protocol = nullptr;
    ProtocolConfig config;
    SystemStateManager state_manager_instance;
    SystemStateManager* state_manager = &state_manager_instance;
    ApplicationAudio* audio = nullptr;
    McpServer* mcp_server = nullptr;
    bool protocol_connected = false;
    bool protocol_ready = false;
    bool pending_listening_start = false;

    ProtocolManager manager(
        nullptr,
        protocol,
        config,
        state_manager,
        audio,
        mcp_server,
        protocol_connected,
        protocol_ready,
        pending_listening_start,
        nullptr);

    WebSocketClient* ws_client = reinterpret_cast<WebSocketClient*>(0x1);
    bool hello_received = true;
    manager.getUiProtocolState(ws_client, hello_received);

    TEST_ASSERT_NULL(ws_client);
    TEST_ASSERT_FALSE(hello_received);
}

int run_protocol_manager_tests(void) {
    int test_count = 0;

    RUN_TEST(test_start_listening_sets_pending_when_mcp_not_ready);
    test_count++;
    RUN_TEST(test_perform_disconnect_resets_flags_and_state);
    test_count++;
    RUN_TEST(test_get_ui_protocol_state_null_when_protocol_missing);
    test_count++;

    return test_count;
}
