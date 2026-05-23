// burn_overlay.cpp — see header for rationale.

#include "burn_overlay.h"
#include "app_config.h"
#include "ui/font_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern SemaphoreHandle_t g_lvglMutex;

namespace {

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_arc = nullptr;
lv_timer_t *s_errorTimer = nullptr;

// Debug-only invariant — show() builds LVGL objects on lv_layer_top, which
// the UI task touches every tick. Caller MUST be on the UI core and MUST
// either hold g_lvglMutex or be running before the mutex exists (boot
// phase). Same pattern as WidgetTagPool::assertUiThreadHoldsLvglMutex
// (#1058 / #1061). F-LO-6 / issue #1014.
inline void assertUiThreadHoldsLvglMutex() {
    configASSERT(xPortGetCoreID() == TASK_CORE_UI);
    configASSERT(g_lvglMutex != nullptr);
    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    configASSERT(holder == nullptr || holder == xTaskGetCurrentTaskHandle());
}

// Drive the indicator's start angle; the bg-angle window keeps the same span
// (kArcSpan), so changing only the start makes the segment chase its tail
// around the ring — standard LVGL 8.3 spinner pattern when LV_USE_SPINNER=0.
constexpr uint16_t kArcSpan = 80;     // degrees of visible indicator
constexpr uint16_t kArcPeriod = 1100; // ms per full rotation

void arcAnimCb(void *var, int32_t value) {
    auto *arc = static_cast<lv_obj_t *>(var);
    const auto start = static_cast<uint16_t>(value % 360);
    const auto end = static_cast<uint16_t>((value + kArcSpan) % 360);
    lv_arc_set_angles(arc, start, end);
}

// Tear down any existing overlay state — animations, timers, and the root
// container. Caller must hold g_lvglMutex.
void teardownOverlay() {
    if (s_errorTimer) {
        lv_timer_del(s_errorTimer);
        s_errorTimer = nullptr;
    }
    if (s_arc) {
        lv_anim_del(s_arc, arcAnimCb);
        s_arc = nullptr;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
}

// Build the full-screen black backdrop on lv_layer_top. Returns the root.
// Shared by show() (spinner state) and showError() (error state).
lv_obj_t *createRoot() {
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    // Solid black backdrop — read as a deliberate "system busy" state rather
    // than a translucent veil that lets the old config show through and looks
    // like a half-broken render.
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
    return root;
}

const char *errorTitleFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "Storage write failed";
        case BurnOverlay::ErrorReason::ReloadFailed:
            return "Config reload failed";
    }
    return "Save failed";
}

const char *errorHintFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "Retry from studio";
        case BurnOverlay::ErrorReason::ReloadFailed:
            return "Reboot to apply";
    }
    return "Retry from studio";
}

// lv_timer callback — runs inside lv_task_handler(), which the UI task
// only invokes while already holding g_lvglMutex. Safe to touch LVGL state.
void errorHoldExpiredCb(lv_timer_t *timer) {
    s_errorTimer = nullptr; // timer auto-deletes after this returns (one-shot)
    (void)timer;
    BurnOverlay::hide();
}

} // namespace

void BurnOverlay::show() {
    assertUiThreadHoldsLvglMutex();
    // Re-show is allowed — tear down any previous instance first.
    teardownOverlay();

    lv_obj_t *root = createRoot();

    // Animated arc — LV_USE_SPINNER is off in this build, so we drive an
    // lv_arc directly with an lv_anim that sweeps the indicator angle.
    static constexpr int16_t kArcSize = 56;
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, kArcSize, kArcSize);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -16);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, kArcSpan);
    lv_arc_set_rotation(arc, 0);
    // Dim background ring + accent indicator (CANShift orange — matches the
    // studio modal tone).
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xE08030), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
    // Hide the draggable knob — we only want the rotating segment.
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, kArcPeriod);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, arcAnimCb);
    lv_anim_start(&a);

    // Status text
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Saving config…");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 36);

    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text(sub, "Writing to storage…");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sub, FontManager::label(12), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 60);

    s_overlay = root;
    s_arc = arc;

    // Force a synchronous redraw so the overlay actually paints before the
    // caller starts the long storage write that would otherwise block all
    // rendering for the duration of the transfer.
    //
    // Task-coupling invariant — read before editing either side:
    //   lv_refr_now() drives DisplayDriver::flushCallback (see
    //   src/hal/display/display_driver.cpp:51) inline on the *calling* task.
    //   In the PUT_CONFIG path that caller is the USB task, not the UI task
    //   (see handlePutConfig in src/hal/usb/usb_comm.cpp). This is safe today
    //   because LVGL is single-threaded as long as g_lvglMutex is held, and
    //   handlePutConfig explicitly takes the mutex before calling show(); SPI
    //   writes from the USB task work the same as from the UI task.
    //
    //   If LVGL is ever reworked for partial-buffer + SPI-DMA, the
    //   flush-complete semaphore would become bound to the calling task and
    //   this assumption breaks — revisit both show() and handlePutConfig at
    //   that point (likely by setting overlay state here and letting the UI
    //   task render on the next tick instead of forcing lv_refr_now).
    lv_refr_now(nullptr);
}

void BurnOverlay::hide() {
    if (!s_overlay && !s_errorTimer)
        return;
    teardownOverlay();
    lv_refr_now(nullptr);
}

void BurnOverlay::showError(ErrorReason reason) {
    // Drop the spinner state but keep the same backdrop colour so the swap
    // doesn't flash through to the dashboard underneath.
    teardownOverlay();

    lv_obj_t *root = createRoot();

    // Big warning glyph at the top — built-in LVGL symbol, no extra font.
    lv_obj_t *icon = lv_label_create(root);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xE04040), 0);
    lv_obj_set_style_text_font(icon, FontManager::secondary(24), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -32);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, errorTitleFor(reason));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 12);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, errorHintFor(reason));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, FontManager::label(12), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);

    s_overlay = root;

    // One-shot teardown — the timer fires from inside lv_task_handler(),
    // which only runs while the UI task holds g_lvglMutex, so the callback
    // is implicitly thread-safe with respect to LVGL state.
    s_errorTimer = lv_timer_create(errorHoldExpiredCb, BURN_OVERLAY_ERROR_HOLD_MS, nullptr);
    if (s_errorTimer) {
        lv_timer_set_repeat_count(s_errorTimer, 1);
    }

    lv_refr_now(nullptr);
}
