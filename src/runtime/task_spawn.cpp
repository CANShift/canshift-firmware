#include "task_spawn.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#include <esp_task_wdt.h>

namespace {

TaskHandle_t trackSpawned(const char *name, TaskHandle_t handle) {
    if (handle == nullptr) {
        LOG_ERROR("TASK", "task '%s' creation failed", name);
        ErrorStore::push(ERROR_SRC_SYSTEM, "task_create", name);
        return nullptr;
    }
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

} // namespace TaskSpawn
