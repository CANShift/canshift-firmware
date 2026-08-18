#pragma once

#include "app_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskSpawn {

TaskHandle_t spawnStatic(const char *name, TaskFunction_t fn, uint32_t stackWords, UBaseType_t prio,
                         StackType_t *stackBuf, StaticTask_t *tcb, BaseType_t core);
TaskHandle_t spawnDynamic(const char *name, TaskFunction_t fn, uint32_t stackWords,
                          UBaseType_t prio, BaseType_t core);

#if APP_PROFILE_UI
void logStackHighWater();
#endif

} // namespace TaskSpawn
