# LVGL ownership

LVGL is single-threaded by contract. Every `lv_*` call on this firmware must
happen on the UI task **OR** under `g_lvglMutex` held by the calling task.
Sources: `src/main.cpp` (taskUI), `src/ui/widgets/widget_tag_pool.cpp`,
`src/diag/lvgl_assert_lock.h`.

## Who runs what

| Task                           | Core | LVGL access policy                                                                                         |
| ------------------------------ | ---- | ---------------------------------------------------------------------------------------------------------- |
| `taskUI`                       | 1    | Owns LVGL by default; takes `g_lvglMutex` around `lv_task_handler` and around widget updates               |
| `taskCAN`                      | 0    | Never touches LVGL. Writes only to `SignalStore` (its own portMUX)                                         |
| `taskUSB`                      | 1    | Must take `g_lvglMutex` before any LVGL call. The PUT_CONFIG burn path holds it for the full storage write |
| `taskBLE`                      | 1    | Same rule as USB: take the mutex before any LVGL call                                                      |
| Arduino `loopTask` (boot only) | —    | Owns LVGL implicitly because taskUI hasn't spawned yet                                                     |

The boot phase is single-threaded by construction (taskUI is spawned at the
end of `BootSequence::run`), so `WidgetTagPool::allocRaw` accepts a null
mutex holder during boot via the explicit `holder == nullptr ||
holder == self` check in `assertUiThreadHoldsLvglMutex()`. Stripping the
null-tolerant branch (#1058) regressed boot (#1061) and was re-added.

## The mutex contract

`g_lvglMutex` is a binary mutex held across:

- `lv_task_handler()` in taskUI's main loop
- Any `lv_*` call from a non-UI task (USB transport, BLE transport, settings, etc.)
- Page navigation triggered from a non-UI task

The wait threshold is 10 ms (`xSemaphoreTake(g_lvglMutex,
pdMS_TO_TICKS(10))` at the taskUI site). `PerfCounters::recordSample`
emits a `LOG_WARN("PERF", "lvgl mutex wait %u µs (>%u)")` when the wait
crosses `PERF_MUTEX_WAIT_WARN_US = 5000` µs — half the timeout, so a
crossing means taskUI is at risk of skipping its own frame.

## Why not `lv_lock`?

LVGL 8.3 ships an opt-in `lv_lock`/`lv_unlock` shim that wraps a
user-provided mutex. We don't use it — `g_lvglMutex` is taken directly so
the wait time can be measured via `PerfCounters` and so the boot-phase
null-holder exception is expressible. If we ever migrate to LVGL 9 the
shim becomes mandatory; the call sites then need a one-line swap from
`xSemaphoreTake(g_lvglMutex, …)` to `lv_lock_timeout(…)`.

## Touch events route through the indev layer, not LVGL events

`GestureController::checkGestures()` reads
`lv_indev_get_gesture_dir(indev)` from taskUI directly instead of
listening for `LV_EVENT_GESTURE`. Reason: buttons frequently absorb
`LV_EVENT_GESTURE` before it bubbles to the screen — the indev layer is
the only reliable cross-button gesture source.

## SettingsPage drag tracker

`SettingsPage` drag is implemented at the indev layer too —
`GestureController::updateDrag` polls the indev state. After release
(`LV_INDEV_STATE_RELEASED`) when drag was tracking, the controller calls
`lv_indev_reset_long_press` + `lv_indev_reset` to clear LVGL's velocity-
based gesture latch. Without this, a swipe-up that closes settings also
opens the diag drawer because both react to `LV_DIR_TOP` on the same
finger motion.

## `lv_refr_now` and task coupling

`BurnOverlay::show()` calls `lv_refr_now(nullptr)` to force a synchronous
redraw before the caller's long storage write blocks rendering. The
flush callback (`DisplayDriver::flushCallback`) runs on the _calling
task_ — for the PUT_CONFIG path that caller is taskUSB. This is safe
because LVGL is single-threaded as long as `g_lvglMutex` is held, and
`handlePutConfig` explicitly takes the mutex around the entire burn
window. SPI writes from taskUSB work the same as from taskUI.

**If LVGL is ever reworked for partial-buffer + SPI-DMA**, the
flush-complete semaphore becomes bound to the calling task and this
assumption breaks — revisit both `BurnOverlay::show()` and
`handlePutConfig` at that point.

## Theme rebuild fast path

`PageManager::updateWidgets()` distinguishes between a full reload
(config changed) and a theme rebuild (`s_rebuildRequested`):

- Reload tears down every page screen and reruns `buildAllPages()`.
- Theme rebuild calls `reapplyThemeAllPages()` which walks the existing
  widget tree and updates style colours in place. The destructive theme-
  toggle rebuild used to push touch→click latency past 200 ms; #1257's
  in-place reskin brings it to sub-ms.

## `LVGL_ASSERT_LOCKED()` canary

`PageManager::navigateTo` and several other entry points assert that
`g_lvglMutex` is held by the calling task. The assertion uses
`xSemaphoreGetMutexHolder` and tolerates the `holder == nullptr` boot
case (same rule as `WidgetTagPool`). `buildUI()` in `boot_sequence.cpp`
takes the mutex briefly around `PageManager::navigateTo` to honour the
contract even though boot is single-threaded.
