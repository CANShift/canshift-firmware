#include "can_task.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "runtime/task_spawn.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

namespace {

constexpr uint32_t kBurstBeforeYield = 16;

StackType_t *s_stack = nullptr;
StaticTask_t s_tcb;
TaskHandle_t s_handle = nullptr;

void taskCAN(void *) {
    uint32_t consecutiveFrames = 0;
    while (true) {
        const bool frameConsumed = CanManager::tick();

        esp_task_wdt_reset();
        if (!frameConsumed) {
            consecutiveFrames = 0;
            continue;
        }
        if (++consecutiveFrames >= kBurstBeforeYield) {
            consecutiveFrames = 0;
            vTaskDelay(CAN_TASK_YIELD_TICKS);
        }
    }
}

} // namespace

bool CanTask::reserveStack() {
    s_stack = static_cast<StackType_t *>(heap_caps_malloc(TASK_STACK_CAN * sizeof(StackType_t),
                                                          MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    return s_stack != nullptr;
}

bool CanTask::start() {
    if (s_handle != nullptr)
        return true;
    s_handle = TaskSpawn::spawnStatic("can", taskCAN, TASK_STACK_CAN, TASK_PRIO_CAN, s_stack,
                                      &s_tcb, TASK_CORE_CAN);
    return s_handle != nullptr;
}

TaskHandle_t CanTask::handle() {
    return s_handle;
}
