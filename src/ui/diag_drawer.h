#pragma once
// diag_drawer.h — Bottom-anchored diagnostic panel showing ECU fault flags,
// key live scalars, and the firmware error log. Issue #635.
//
// Same `lv_layer_top()` layer as `ErrorBar` so it can cover the dashboard
// without participating in page transitions. A small clickable "handle"
// strip at the very bottom of the screen toggles the panel; v1 deliberately
// uses tap-to-open instead of swipe-up to side-step the existing ErrorBar
// gesture (follow-up: layer in swipe gestures with deconflict).

namespace DiagDrawer {

/**
 * Build the panel + handle on `lv_layer_top()`. Idempotent — calling it
 * twice is a no-op. Must run on the UI task while the LVGL mutex is held
 * (PageManager owns that contract today).
 */
void init();

/**
 * Refresh the panel's labels from `SignalStore` + `ErrorStore`. Cheap when
 * the panel is hidden — every UI tick is fine. Same call site as
 * `ErrorBar::update()`.
 */
void update();

/** Programmatic open/close — used by the handle tap callback and tests. */
void open();
void close();

} // namespace DiagDrawer
