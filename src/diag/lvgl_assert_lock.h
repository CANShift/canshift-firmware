#pragma once

#include "app_config.h"

#if APP_DEBUG_BUILD || APP_PROFILE_UI

    #include <freertos/FreeRTOS.h>
    #include <freertos/semphr.h>
    #include <freertos/task.h>

    #include <esp_system.h>
    #include <stdio.h>

extern SemaphoreHandle_t g_lvglMutex;

namespace LvglLock {

inline void assertLocked(const char *file, int line, const char *fn) {

    if (g_lvglMutex == nullptr)
        return;

    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (holder != self) {

        printf("[LVGL_ASSERT_LOCKED] FAIL: lv_* without g_lvglMutex — %s:%d %s "
               "(holder=%p self=%p)\n",
               file, line, fn, static_cast<void *>(holder), static_cast<void *>(self));
        abort();
    }
}

} // namespace LvglLock

    #define LVGL_ASSERT_LOCKED() ::LvglLock::assertLocked(__FILE__, __LINE__, __func__)

#else

    #define LVGL_ASSERT_LOCKED() ((void)0)

#endif
