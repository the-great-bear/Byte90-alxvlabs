/**
 * TaskManager.cpp
 *
 * Implementation of centralized FreeRTOS task management
 */

#include "TaskManager.h"
#include <esp_log.h>

static const char* TAG = "TaskManager";

// Singleton instance
TaskManager& TaskManager::instance() {
    static TaskManager instance;
    return instance;
}

// Constructor
TaskManager::TaskManager()
    : _mutex(nullptr)
    , _initialized(false)
    , _last_health_check_ms(0)
{
}

// Initialize
bool TaskManager::begin() {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    _initialized = true;
    _last_health_check_ms = millis();

    ESP_LOGI(TAG, "TaskManager initialized");
    return true;
}

// Create task
bool TaskManager::createTask(
    const char* name,
    const char* component,
    TaskFunction_t task_function,
    void* parameter,
    UBaseType_t priority,
    BaseType_t core,
    uint32_t stack_size,
    CleanupPattern cleanup_pattern,
    const char* description,
    uint32_t cleanup_timeout_ms
) {
    if (!_initialized) {
        ESP_LOGE(TAG, "TaskManager not initialized");
        return false;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Check if task already exists
    auto existing = _tasks.find(name);
    if (existing != _tasks.end()) {
        if (!existing->second.is_active || existing->second.handle == nullptr) {
            // Allow recreation of inactive tasks (one-shot or completed)
            _tasks.erase(existing);
        } else {
            ESP_LOGE(TAG, "Task '%s' already exists", name);
            xSemaphoreGive(_mutex);
            return false;
        }
    }

    // Create the task
    TaskHandle_t handle = nullptr;
    BaseType_t result = xTaskCreatePinnedToCore(
        task_function,
        name,
        stack_size,
        parameter,
        priority,
        &handle,
        core
    );

    if (result != pdPASS || handle == nullptr) {
        ESP_LOGE(TAG, "Failed to create task '%s'", name);
        xSemaphoreGive(_mutex);
        return false;
    }

    // Store metadata
    TaskMetadata metadata;
    metadata.name = name;
    metadata.component = component;
    metadata.handle = handle;
    metadata.priority = priority;
    metadata.core = core;
    metadata.stack_size = stack_size;
    metadata.cleanup_pattern = cleanup_pattern;
    metadata.description = description ? description : "";
    metadata.created_at_ms = millis();
    metadata.last_seen_ms = millis();
    metadata.high_water_mark = 0;
    metadata.is_active = true;
    metadata.cleanup_timeout_ms = cleanup_timeout_ms;

    _tasks[name] = metadata;

    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "Created task '%s' (%s) - priority=%d, core=%d, stack=%uKB",
             name, component, priority, core, stack_size / 1024);
    return true;
}

// Stop task
void TaskManager::stopTask(const char* name) {
    if (!_initialized) {
        ESP_LOGE(TAG, "TaskManager not initialized");
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    TaskMetadata* task = findTask(name);
    if (!task) {
        ESP_LOGW(TAG, "Task '%s' not found", name);
        xSemaphoreGive(_mutex);
        return;
    }

    if (!task->handle) {
        ESP_LOGW(TAG, "Task '%s' already stopped", name);
        xSemaphoreGive(_mutex);
        return;
    }

    TaskHandle_t handle = task->handle;
    CleanupPattern pattern = task->cleanup_pattern;
    uint32_t timeout_ms = task->cleanup_timeout_ms;

    xSemaphoreGive(_mutex);

    auto wait_for_exit = [handle](uint32_t wait_ms) -> bool {
        if (!handle) {
            return true;
        }
        uint32_t ticks = pdMS_TO_TICKS(wait_ms);
        uint32_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < ticks) {
            eTaskState state = eTaskGetState(handle);
            if (state == eDeleted || state == eInvalid) {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return false;
    };

    auto resolve_timeout = [pattern, timeout_ms]() -> uint32_t {
        if (timeout_ms > 0) {
            return timeout_ms;
        }
        switch (pattern) {
            case CleanupPattern::GRACEFUL:
                return 2000;
            case CleanupPattern::GRACEFUL_THEN_FORCE:
                return 100;
            case CleanupPattern::SELF_DELETING:
                return 1000;
            case CleanupPattern::FORCE_DELETE:
            default:
                return 0;
        }
    };

    uint32_t resolved_timeout_ms = resolve_timeout();

    // Execute cleanup pattern
    switch (pattern) {
        case CleanupPattern::GRACEFUL: {
            // Poll with timeout (AudioService pattern)
            ESP_LOGI(TAG, "Stopping task '%s' gracefully...", name);
            bool exited = wait_for_exit(resolved_timeout_ms);

            if (!exited) {
                ESP_LOGW(TAG, "Task '%s' did not exit gracefully within %u ms timeout",
                         name, resolved_timeout_ms);
                // Note: We don't force delete - component may need more time
            } else {
                ESP_LOGI(TAG, "Task '%s' exited gracefully", name);
            }
            break;
        }

        case CleanupPattern::GRACEFUL_THEN_FORCE: {
            ESP_LOGI(TAG, "Stopping task '%s' gracefully (timeout=%u ms)...",
                     name, resolved_timeout_ms);
            bool exited = wait_for_exit(resolved_timeout_ms);
            if (!exited) {
                ESP_LOGW(TAG, "Task '%s' did not exit in %u ms - force deleting",
                         name, resolved_timeout_ms);
                vTaskDelete(handle);
                ESP_LOGI(TAG, "Task '%s' force deleted", name);
            } else {
                ESP_LOGI(TAG, "Task '%s' exited gracefully", name);
            }
            break;
        }

        case CleanupPattern::FORCE_DELETE: {
            if (handle) {
                eTaskState state = eTaskGetState(handle);
                if (state == eDeleted || state == eInvalid) {
                    ESP_LOGI(TAG, "Task '%s' already deleted", name);
                    break;
                }
            }
            ESP_LOGI(TAG, "Force deleting task '%s'...", name);
            vTaskDelete(handle);
            ESP_LOGI(TAG, "Task '%s' deleted", name);
            break;
        }

        case CleanupPattern::SELF_DELETING: {
            // Just wait for task to self-exit
            ESP_LOGI(TAG, "Waiting for task '%s' to self-delete...", name);
            bool exited = wait_for_exit(resolved_timeout_ms);
            if (exited) {
                ESP_LOGI(TAG, "Task '%s' self-deleted", name);
            } else {
                ESP_LOGW(TAG, "Task '%s' did not self-delete within %u ms",
                         name, resolved_timeout_ms);
            }
            break;
        }
    }

    // Mark as inactive
    xSemaphoreTake(_mutex, portMAX_DELAY);
    task = findTask(name);
    if (task) {
        task->handle = nullptr;
        task->is_active = false;
    }
    xSemaphoreGive(_mutex);
}

void TaskManager::markTaskStopped(const char* name) {
    if (!_initialized) {
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    TaskMetadata* task = findTask(name);
    if (task) {
        task->handle = nullptr;
        task->is_active = false;
    }
    xSemaphoreGive(_mutex);
}

// Check if task is active
bool TaskManager::isTaskActive(const char* name) const {
    if (!_initialized) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    const TaskMetadata* task = findTask(name);
    bool active = task && task->handle != nullptr && task->is_active;
    xSemaphoreGive(_mutex);

    return active;
}

// Get task info
const TaskMetadata* TaskManager::getTaskInfo(const char* name) const {
    if (!_initialized) return nullptr;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    const TaskMetadata* task = findTask(name);
    xSemaphoreGive(_mutex);

    return task;
}

// Get tasks by component
std::vector<const char*> TaskManager::getTasksByComponent(const char* component) const {
    std::vector<const char*> result;

    if (!_initialized) return result;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    for (const auto& pair : _tasks) {
        if (strcmp(pair.second.component, component) == 0) {
            result.push_back(pair.second.name);
        }
    }

    xSemaphoreGive(_mutex);
    return result;
}

// Get task count
size_t TaskManager::getTaskCount() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t count = _tasks.size();
    xSemaphoreGive(_mutex);

    return count;
}

// Get active task count
size_t TaskManager::getActiveTaskCount() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    size_t count = 0;
    for (const auto& pair : _tasks) {
        if (pair.second.handle != nullptr && pair.second.is_active) {
            count++;
        }
    }

    xSemaphoreGive(_mutex);
    return count;
}

// Get total stack memory
uint32_t TaskManager::getTotalStackMemory() const {
    if (!_initialized) return 0;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    uint32_t total = 0;
    for (const auto& pair : _tasks) {
        total += pair.second.stack_size;
    }

    xSemaphoreGive(_mutex);
    return total;
}

// Check stack health
int TaskManager::checkStackHealth(uint32_t threshold_bytes) {
    if (!_initialized) return 0;

    updateTaskHealth();

    xSemaphoreTake(_mutex, portMAX_DELAY);

    int warning_count = 0;
    for (auto& pair : _tasks) {
        TaskMetadata& task = pair.second;
        if (!task.handle || !task.is_active) continue;

        // high_water_mark is in words (4 bytes each)
        uint32_t free_bytes = task.high_water_mark * 4;

        if (free_bytes < threshold_bytes) {
            ESP_LOGW(TAG, "Task '%s' low stack: %u bytes remaining (threshold=%u)",
                     task.name, free_bytes, threshold_bytes);
            warning_count++;
        }
    }

    xSemaphoreGive(_mutex);
    return warning_count;
}

// Detect stuck tasks
int TaskManager::detectStuckTasks(uint32_t timeout_ms) {
    if (!_initialized) return 0;

    uint32_t now = millis();

    xSemaphoreTake(_mutex, portMAX_DELAY);

    int stuck_count = 0;
    for (auto& pair : _tasks) {
        TaskMetadata& task = pair.second;
        if (!task.handle || !task.is_active) continue;

        uint32_t elapsed = now - task.last_seen_ms;
        if (elapsed > timeout_ms) {
            ESP_LOGW(TAG, "Task '%s' not seen for %u ms - may be stuck!",
                     task.name, elapsed);
            stuck_count++;
        }
    }

    xSemaphoreGive(_mutex);
    return stuck_count;
}

// Print health report
void TaskManager::printHealthReport() {
    if (!_initialized) {
        ESP_LOGW(TAG, "TaskManager not initialized");
        return;
    }

    updateTaskHealth();

    xSemaphoreTake(_mutex, portMAX_DELAY);

    size_t total = _tasks.size();
    size_t active = 0;
    uint32_t total_stack = 0;
    for (const auto& pair : _tasks) {
        total_stack += pair.second.stack_size;
        if (pair.second.handle != nullptr && pair.second.is_active) {
            active++;
        }
    }

    ESP_LOGI(TAG, ":::: Task Health Report ::::");
    int low_stack = 0;
    int stuck = 0;
    for (const auto& pair : _tasks) {
        const TaskMetadata& task = pair.second;
        if (!task.handle || !task.is_active) {
            continue;
        }
        uint32_t free_bytes = task.high_water_mark * 4;
        if (free_bytes < 512) {
            low_stack++;
        }
        if (millis() - task.last_seen_ms > 10000) {
            stuck++;
        }
    }
    ESP_LOGI(TAG, "Tasks: total=%u active=%u low_stack=%d stuck=%d",
             total, active, low_stack, stuck);
    ESP_LOGI(TAG, ":::: End Task Report ::::");

    xSemaphoreGive(_mutex);
}

// List tasks
void TaskManager::listTasks() {
    printHealthReport();
}

// Periodic loop
void TaskManager::loop() {
    if (!_initialized) return;

    uint32_t now = millis();

    // Run health checks every 30 seconds
    if (now - _last_health_check_ms < 30000) {
        return;
    }

    _last_health_check_ms = now;

    // Update task health
    updateTaskHealth();

    // Check for stack warnings
    int low_stack = checkStackHealth(512);
    if (low_stack > 0) {
        ESP_LOGW(TAG, "%d task(s) have low stack!", low_stack);
    }

    // Check for stuck tasks
    int stuck = detectStuckTasks(10000);
    if (stuck > 0) {
        ESP_LOGW(TAG, "%d task(s) may be stuck!", stuck);
    }

    // Health report logging moved to SystemState transitions
}

// Update task health (private)
void TaskManager::updateTaskHealth() {
    if (!_initialized) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    uint32_t now = millis();

    for (auto& pair : _tasks) {
        TaskMetadata& task = pair.second;

        if (task.handle && task.is_active) {
            // Update stack high water mark
            task.high_water_mark = uxTaskGetStackHighWaterMark(task.handle);
            task.last_seen_ms = now;
        }
    }

    xSemaphoreGive(_mutex);
}

// Find task by name (private, non-const)
TaskMetadata* TaskManager::findTask(const char* name) {
    auto it = _tasks.find(name);
    if (it != _tasks.end()) {
        return &(it->second);
    }
    return nullptr;
}

// Find task by name (private, const)
const TaskMetadata* TaskManager::findTask(const char* name) const {
    auto it = _tasks.find(name);
    if (it != _tasks.end()) {
        return &(it->second);
    }
    return nullptr;
}
