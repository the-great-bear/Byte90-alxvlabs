#include <unity.h>

#include "McpToolManager.h"
#include "TaskManager.h"
#include "../test_mcp_tool_manager.h"

static void test_enter_and_exit_loading_restores_previous_state() {
    SystemStateManager state_manager;
    state_manager.setState(SYSTEM_STATE_IDLE);

    McpToolManager manager(&state_manager, nullptr);

    manager.enterToolLoading();
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_LOADING, state_manager.getState());

    manager.exitToolLoading();
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, state_manager.getState());
}

static void test_nested_loading_depth_requires_balanced_exits() {
    SystemStateManager state_manager;
    state_manager.setState(SYSTEM_STATE_CONNECTING);

    McpToolManager manager(&state_manager, nullptr);

    manager.enterToolLoading();
    manager.enterToolLoading();
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_LOADING, state_manager.getState());

    manager.exitToolLoading();
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_LOADING, state_manager.getState());

    manager.exitToolLoading();
    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_CONNECTING, state_manager.getState());
}

static void test_enqueue_without_worker_is_safe() {
    SystemStateManager state_manager;
    McpToolManager manager(&state_manager, nullptr);

    manager.enqueueRequest("{\"method\":\"tools/list\"}", "session", true);

    TEST_ASSERT_EQUAL_INT(SYSTEM_STATE_IDLE, state_manager.getState());
}

static void test_ensure_worker_creates_task() {
    TEST_ASSERT_TRUE(TaskManager::instance().begin());

    SystemStateManager state_manager;
    {
        McpToolManager manager(&state_manager, nullptr);
        manager.ensureWorker();
        delay(50);
        TEST_ASSERT_TRUE(TaskManager::instance().isTaskActive("mcp_tool"));
    }

    delay(50);
    TEST_ASSERT_FALSE(TaskManager::instance().isTaskActive("mcp_tool"));
}

int run_mcp_tool_manager_tests(void) {
    int test_count = 0;

    RUN_TEST(test_enter_and_exit_loading_restores_previous_state);
    test_count++;
    RUN_TEST(test_nested_loading_depth_requires_balanced_exits);
    test_count++;
    RUN_TEST(test_enqueue_without_worker_is_safe);
    test_count++;
    RUN_TEST(test_ensure_worker_creates_task);
    test_count++;

    return test_count;
}
