#include <unity.h>
#include <cstring>

#include "TaskManager.h"
#include "../test_task_manager.h"

namespace {
volatile bool g_blocking_task_run = true;

void blockingTask(void* /*param*/) {
    while (g_blocking_task_run) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(nullptr);
}

void selfDeletingTask(void* /*param*/) {
    TaskManager::instance().markTaskStopped("tm_self_delete");
    vTaskDelete(nullptr);
}
}

static void test_create_duplicate_task_is_rejected() {
    TEST_ASSERT_TRUE(TaskManager::instance().begin());

    g_blocking_task_run = true;
    bool created_first = TaskManager::instance().createTask(
        "tm_duplicate",
        "TaskManagerTest",
        blockingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::FORCE_DELETE,
        "duplicate rejection test"
    );
    TEST_ASSERT_TRUE(created_first);

    bool created_second = TaskManager::instance().createTask(
        "tm_duplicate",
        "TaskManagerTest",
        blockingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::FORCE_DELETE,
        "duplicate rejection test"
    );
    TEST_ASSERT_FALSE(created_second);

    TaskManager::instance().stopTask("tm_duplicate");
}

static void test_task_can_be_recreated_after_stop() {
    TEST_ASSERT_TRUE(TaskManager::instance().begin());

    g_blocking_task_run = true;
    TEST_ASSERT_TRUE(TaskManager::instance().createTask(
        "tm_recreate",
        "TaskManagerTest",
        blockingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::FORCE_DELETE,
        "recreate test"
    ));

    TaskManager::instance().stopTask("tm_recreate");

    g_blocking_task_run = true;
    TEST_ASSERT_TRUE(TaskManager::instance().createTask(
        "tm_recreate",
        "TaskManagerTest",
        blockingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::FORCE_DELETE,
        "recreate test"
    ));

    TaskManager::instance().stopTask("tm_recreate");
}

static void test_self_deleting_task_becomes_inactive() {
    TEST_ASSERT_TRUE(TaskManager::instance().begin());

    TEST_ASSERT_TRUE(TaskManager::instance().createTask(
        "tm_self_delete",
        "TaskManagerTest",
        selfDeletingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::SELF_DELETING,
        "self delete test"
    ));

    delay(50);

    TEST_ASSERT_FALSE(TaskManager::instance().isTaskActive("tm_self_delete"));

    const TaskMetadata* info = TaskManager::instance().getTaskInfo("tm_self_delete");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_FALSE(info->is_active);
}

static void test_get_tasks_by_component_includes_created_task() {
    TEST_ASSERT_TRUE(TaskManager::instance().begin());

    g_blocking_task_run = true;
    TEST_ASSERT_TRUE(TaskManager::instance().createTask(
        "tm_component_lookup",
        "TaskManagerLookup",
        blockingTask,
        nullptr,
        1,
        1,
        4096,
        CleanupPattern::FORCE_DELETE,
        "component lookup test"
    ));

    std::vector<const char*> tasks = TaskManager::instance().getTasksByComponent("TaskManagerLookup");

    bool found = false;
    for (const char* name : tasks) {
        if (name && strcmp(name, "tm_component_lookup") == 0) {
            found = true;
            break;
        }
    }

    TEST_ASSERT_TRUE(found);

    TaskManager::instance().stopTask("tm_component_lookup");
}

int run_task_manager_tests(void) {
    int test_count = 0;

    RUN_TEST(test_create_duplicate_task_is_rejected);
    test_count++;
    RUN_TEST(test_task_can_be_recreated_after_stop);
    test_count++;
    RUN_TEST(test_self_deleting_task_becomes_inactive);
    test_count++;
    RUN_TEST(test_get_tasks_by_component_includes_created_task);
    test_count++;

    return test_count;
}
