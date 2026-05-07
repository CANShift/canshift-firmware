// burn_overlay.cpp — see header for rationale.

#include "burn_overlay.h"
#include "ui/font_manager.h"

namespace {

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_arc = nullptr;

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

} // namespace

void BurnOverlay::show() {
    // Re-show is allowed — tear down any previous instance first.
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
        s_arc = nullptr;
    }

    // Full-screen container on lv_layer_top so it floats above the active page.
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
    lv_obj_set_style_text_font(title, FontManager::get(16), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 36);

    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text(sub, "Writing to SD…");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sub, FontManager::get(12), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 60);

    s_overlay = root;
    s_arc = arc;

    // Force a synchronous redraw so the overlay actually paints before the
    // caller starts the long SD write that would otherwise block all
    // rendering for the duration of the transfer.
    lv_refr_now(nullptr);
}

void BurnOverlay::hide() {
    if (!s_overlay)
        return;
    if (s_arc) {
        lv_anim_del(s_arc, arcAnimCb);
        s_arc = nullptr;
    }
    lv_obj_del(s_overlay);
    s_overlay = nullptr;
    lv_refr_now(nullptr);
}
