// alert_flash.cpp — implementation of the red threshold-flash overlay.

#include "alert_flash.h"

#include <cmath>

namespace AlertFlash {

namespace {

constexpr uint32_t FLASH_RGB = 0xFF2222; // Bright red, leans saturated for contrast
constexpr uint8_t FLASH_PEAK_OPA = 180;  // ~70 % opacity at the peak of each pulse
constexpr uint16_t HALF_CYCLE_MS = 500;  // 500 ms up + 500 ms down = 1 Hz pulse
constexpr uint32_t LABEL_ALERT_RGB = 0xFFFFFF;

void overlayOpaCb(void *obj, int32_t v) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void startAnim(lv_obj_t *overlay) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, overlay);
    lv_anim_set_values(&a, 0, FLASH_PEAK_OPA);
    lv_anim_set_time(&a, HALF_CYCLE_MS);
    lv_anim_set_playback_time(&a, HALF_CYCLE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, overlayOpaCb);
    lv_anim_start(&a);
}

void stopAnim(lv_obj_t *overlay) {
    lv_anim_del(overlay, overlayOpaCb);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, LV_PART_MAIN);
}

void setLabelColors(State &s, uint32_t rgb) {
    for (uint8_t i = 0; i < s.labelCount; ++i) {
        if (s.labels[i] != nullptr) {
            lv_obj_set_style_text_color(s.labels[i], lv_color_hex(rgb), 0);
        }
    }
}

void restoreLabelColors(State &s) {
    for (uint8_t i = 0; i < s.labelCount; ++i) {
        if (s.labels[i] != nullptr) {
            lv_obj_set_style_text_color(s.labels[i], lv_color_hex(s.restoreRgb[i]), 0);
        }
    }
}

} // namespace

void attach(State &s, lv_obj_t *container) {
    s.overlay = lv_obj_create(container);
    lv_obj_remove_style_all(s.overlay);
    lv_obj_clear_flag(s.overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_coord_t w = lv_obj_get_width(container);
    lv_coord_t h = lv_obj_get_height(container);
    lv_obj_set_size(s.overlay, w, h);
    lv_obj_set_pos(s.overlay, 0, 0);
    lv_obj_set_style_bg_color(s.overlay, lv_color_hex(FLASH_RGB), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s.overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s.overlay, 0, LV_PART_MAIN);
    s.active = false;
    s.labelCount = 0;
}

void watchLabel(State &s, lv_obj_t *label, uint32_t restoreRgb) {
    if (label == nullptr || s.labelCount >= MAX_TRACKED_LABELS)
        return;
    s.labels[s.labelCount] = label;
    s.restoreRgb[s.labelCount] = restoreRgb;
    ++s.labelCount;
}

void update(State &s, float value, float threshold) {
    if (s.overlay == nullptr)
        return;

    const bool shouldBeActive = !std::isnan(threshold) && value > threshold;

    if (shouldBeActive == s.active)
        return;
    s.active = shouldBeActive;

    if (shouldBeActive) {
        startAnim(s.overlay);
        setLabelColors(s, LABEL_ALERT_RGB);
    } else {
        stopAnim(s.overlay);
        restoreLabelColors(s);
    }
}

} // namespace AlertFlash
