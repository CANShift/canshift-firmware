#pragma once
// gesture_controller.h — Touch-gesture state machines for the dashboard UI.
//
// Owns three independent input flows that previously lived inside
// `page_manager.cpp` (issue #704):
//
//   - LVGL swipe gesture polling. Detects LEFT / RIGHT page-nav swipes and
//     forwards them to a `SwipeHandler` callback registered by the caller.
//     Keeps gesture-direction tracking deduplicated across LVGL ticks so a
//     single sustained swipe doesn't refire navigation.
//
//   - Settings-panel drag tracker. Translates a top-edge drag into a
//     panel-position update via SettingsPage, then snaps open or closed at
//     release based on a 50 %-of-distance threshold.
//
//   - Swipe-click cancellation. When a finger moves more than
//     SWIPE_CANCEL_THRESHOLD_PX horizontally, clears LVGL's pressed object so
//     button widgets under the swipe path don't fire on release (issue #640).

#include <lvgl.h>

namespace GestureController {

/**
 * Callback fired when a page-nav swipe is detected. The caller (PageManager)
 * decides how to react — typically `showPage(prev/next, anim)`.
 */
using SwipeHandler = void (*)(lv_dir_t direction);

/**
 * Register the swipe handler. Idempotent; pass `nullptr` to detach. Must be
 * called before `checkGestures()` first runs.
 */
void setSwipeHandler(SwipeHandler handler);

/**
 * Callback fired on a vertical swipe (LV_DIR_TOP / LV_DIR_BOTTOM). Used by
 * DiagDrawer to open on a swipe-up gesture without needing its own input
 * obj — relying on the polling path here is more reliable than LVGL's
 * event-system gestures on a transparent overlay (see #635 follow-up).
 */
using VerticalSwipeHandler = void (*)(lv_dir_t direction);

/**
 * Register the vertical swipe handler. Idempotent; pass `nullptr` to
 * detach. Same dispatch path as the horizontal handler — polled once per
 * LVGL tick from `checkGestures()`.
 */
void setVerticalSwipeHandler(VerticalSwipeHandler handler);

/**
 * Poll the input device for gesture / drag / click-cancel events. Call once
 * per LVGL tick from `PageManager::updateWidgets()`. Internally walks every
 * pointer indev so it works whether touch is plugged into LVGL via the
 * primary or a secondary device.
 */
void checkGestures();

} // namespace GestureController
