#include "can_task.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "runtime/task_spawn.h"

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

namespace {

constexpr uint32_t kBurstBeforeYield = 16;
constexpr uint32_t kWatchdogFeedPeriodMs = 100;

StackType_t *s_stack = nullptr;
StaticTask_t s_tcb;
TaskHandle_t s_handle = nullptr;

void feedWatchdogEvery(uint32_t &lastFeedMs) {
    static bool s_feedFailureLogged = false;
    const uint32_t nowMs = millis();
    if (nowMs - lastFeedMs < kWatchdogFeedPeriodMs)
        return;
    lastFeedMs = nowMs;
    const esp_err_t err = esp_task_wdt_reset();
    if (err == ESP_OK || s_feedFailureLogged)
        return;
    s_feedFailureLogged = true;
    LOG_WARN("CAN", "task watchdog feed failed: %s", esp_err_to_name(err));
}

void taskCAN(void *) {
    uint32_t consecutiveFrames = 0;
    uint32_t lastFeedMs = 0;
    while (true) {
        const bool frameConsumed = CanManager::tick();

        feedWatchdogEvery(lastFeedMs);
        if (!frameConsumed) {
            consecutiveFrames = 0;
            continue;
        }
        if (++consecutiveFrames >= kBurstBeforeYield) {
            consecutiveFrames = 0;
            vTaskDelay(kCanTaskYieldTicks);
        }
    }
}

}

bool CanTask::reserveStack() {
    s_stack = static_cast<StackType_t *>(heap_caps_malloc(TASK_STACK_CAN * sizeof(StackType_t),
                                                          MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    return s_stack != nullptr;
}

bool CanTask::start() {
    if (s_handle != nullptr)
        return true;
    s_handle = TaskSpawn::spawnStatic("can", taskCAN, TASK_STACK_CAN, kTaskPrioCan, s_stack, &s_tcb,
                                      kTaskCoreCan);
    return s_handle != nullptr;
}

TaskHandle_t CanTask::handle() {
    return s_handle;
}
