#pragma once
// lvgl_assert_lock.h — Debug-only invariant assert for LVGL mutex coverage.
//
// LVGL is NOT thread-safe. Every `lv_*` call must run with `g_lvglMutex`
// held by the calling task. The current code base maintains this discipline
// rigorously (#158 audit, 2026-05-31 — 1495 `lv_*` callsites across 37 .cpp
// files, no violations found), but the discipline relies on convention
// rather than language enforcement. A future PR that calls an `lv_*` symbol
// from a non-UI task — or that forgets to take the mutex inside a new
// helper — would silently race, with symptoms ranging from glitched frames
// to NULL-deref crashes inside `lv_obj_invalidate`.
//
// `LVGL_ASSERT_LOCKED()` makes the rule loud: in debug builds it crashes
// immediately when invoked without the mutex held, with a backtrace and the
// holder / self task handles. In release builds it compiles to a no-op.
//
// Wire it at the top of any function that touches LVGL state from a
// less-obvious context — page transitions, widget rebuild, theme apply,
// any one-shot mutation. Don't sprinkle it inside hot per-frame helpers
// (cost adds up); a canary on a few entry points is enough to catch a
// regression at PR review time.

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
    // Pre-mutex-init phase (boot setup runs before `xSemaphoreCreateMutex`
    // in `setup()`). Boot-time code holds the single main thread — no race
    // possible, so the assert is moot.
    if (g_lvglMutex == nullptr)
        return;

    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (holder != self) {
        // Use printf — LOG_* might allocate or take another mutex, and we
        // are about to abort anyway.
        printf("[LVGL_ASSERT_LOCKED] FAIL: lv_* without g_lvglMutex — %s:%d %s "
               "(holder=%p self=%p)\n",
               file, line, fn, static_cast<void *>(holder), static_cast<void *>(self));
        abort();
    }
}

} // namespace LvglLock

    #define LVGL_ASSERT_LOCKED() ::LvglLock::assertLocked(__FILE__, __LINE__, __func__)

#else // !APP_DEBUG_BUILD && !APP_PROFILE_UI

    /** Release builds: zero-cost no-op. */
    #define LVGL_ASSERT_LOCKED() ((void)0)

#endif
