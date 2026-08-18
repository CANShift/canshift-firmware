#include "task_spawn.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#include <esp_task_wdt.h>
#include <stdio.h>

namespace {

#if APP_PROFILE_UI
constexpr size_t kTrackedTaskCap = 8;

struct TrackedTask {
    const char *name;
    TaskHandle_t handle;
};

TrackedTask s_tracked[kTrackedTaskCap] = {};
size_t s_trackedCount = 0;

void remember(const char *name, TaskHandle_t handle) {
    if (s_trackedCount >= kTrackedTaskCap) {
        LOG_WARN("TASK", "stack watermark registry full — '%s' not tracked", name);
        return;
    }
    s_tracked[s_trackedCount++] = {name, handle};
}

#endif

TaskHandle_t trackSpawned(const char *name, TaskHandle_t handle) {
    if (handle == nullptr) {
        LOG_ERROR("TASK", "task '%s' creation failed", name);
        ErrorStore::push(ERROR_SRC_SYSTEM, "task_create", name);
        return nullptr;
    }
#if APP_PROFILE_UI
    remember(name, handle);
#endif
    const esp_err_t err = esp_task_wdt_add(handle);
    if (err != ESP_OK) {
        LOG_WARN("TASK", "WDT add(%s) failed: %d", name, static_cast<int>(err));
    }
    return handle;
}

} // namespace

namespace TaskSpawn {

TaskHandle_t spawnStatic(const char *name, TaskFunction_t fn, uint32_t stackWords, UBaseType_t prio,
                         StackType_t *stackBuf, StaticTask_t *tcb, BaseType_t core) {
    return trackSpawned(name, xTaskCreateStaticPinnedToCore(fn, name, stackWords, nullptr, prio,
                                                            stackBuf, tcb, core));
}

TaskHandle_t spawnDynamic(const char *name, TaskFunction_t fn, uint32_t stackWords,
                          UBaseType_t prio, BaseType_t core) {
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(fn, name, stackWords, nullptr, prio, &handle, core) != pdPASS) {
        handle = nullptr;
    }
    return trackSpawned(name, handle);
}

#if APP_PROFILE_UI
void logStackHighWater() {
    char buf[160];
    int written = 0;
    for (size_t i = 0; i < s_trackedCount; ++i) {
        const unsigned freeWords =
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(s_tracked[i].handle));
        const int n = snprintf(buf + written, sizeof(buf) - static_cast<size_t>(written), "%s=%u ",
                               s_tracked[i].name, freeWords);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(buf) - static_cast<size_t>(written))
            break;
        written += n;
    }
    if (written == 0)
        return;
    LOG_INFO("PERF", "stack headroom (words): %s", buf);
}
#endif

} // namespace TaskSpawn
