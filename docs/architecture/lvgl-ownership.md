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
| Arduino `loopTask` (boot only) | —    | Owns LVGL implicitly because taskUI hasn't spawned yet; deletes itself at the end of `setup()`             |

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
crosses `kPerfMutexWaitWarnUs = 5000` µs — half the timeout, so a
crossing means taskUI is at risk of skipping its own frame.

## Cross-task handoff

- Work that needs LVGL from another task goes through `PendingActions` and is
  drained by taskUI. `ActionDispatcher` runs in the input task and never calls
  `lv_async_call` either, because that mutates LVGL's timer list without the
  mutex.
- Blocking work runs in taskUI's pre-mutex drain, never inside
  `lv_task_handler()`: touch calibration blocks until four corners are tapped,
  so `TouchDriver::calibrate` also unsubscribes its task from the watchdog for
  the duration.
- `PendingActions` slots are single-writer, last-write-wins. Each payload is
  written before its flag with release/acquire ordering, so taskUI never sees
  a flag before its buffer, a version against a later size, or a reason with
  the detail of a later failure.
- A page screen is deleted only once it is no longer the active screen;
  deleting the active one leaves LVGL's active-screen pointer dangling.

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

LVGL drops gesture detection while a scroll is live (`indev_gesture` returns
when `scroll_obj` is set), and the settings panel scrolls. Its close swipe is
therefore measured by `GestureIntent` from raw travel. A page turn needs a
horizontally dominated travel; one that starts on a clickable must also clear
`kSwipeThroughTravelPx`, so a swipe outranks the tap the finger already began.

## SettingsPage drag tracker

`SettingsPage` drag is implemented at the indev layer too —
`GestureController::updateDrag` polls the indev state. After release
(`LV_INDEV_STATE_RELEASED`) when drag was tracking, the controller calls
`lv_indev_reset_long_press` + `lv_indev_reset` to clear LVGL's velocity-
based gesture latch, so the motion that closes Settings is not also read
as a fresh gesture. Nothing consumes vertical swipes since the diag
drawer was removed; the settings drag is a pointer drag, not a gesture.

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
