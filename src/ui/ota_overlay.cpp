#include "ota_overlay.h"
#include "ui/font_manager.h"

#include <atomic>
#include <lvgl.h>
#include <stdio.h>

namespace {

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_arc = nullptr;
lv_obj_t *s_progressBar = nullptr;
lv_obj_t *s_subLabel = nullptr;
lv_obj_t *s_breathObj = nullptr;
lv_timer_t *s_arcTimer = nullptr;

std::atomic<size_t> s_total{0};
std::atomic<size_t> s_written{0};
size_t s_lastRenderedWritten = SIZE_MAX;

constexpr uint16_t kArcSpan = 80;
constexpr uint32_t kArcTickMs = 16;
constexpr uint16_t kArcAdvanceDeg = 6;
uint16_t s_arcAngle = 0;

void arcTimerCb(lv_timer_t *timer) {
    auto *arc = static_cast<lv_obj_t *>(timer->user_data);
    s_arcAngle = static_cast<uint16_t>((s_arcAngle + kArcAdvanceDeg) % 360);
    const auto end = static_cast<uint16_t>((s_arcAngle + kArcSpan) % 360);
    lv_arc_set_angles(arc, s_arcAngle, end);
}

void breathCb(void *obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
}

void teardownOverlay() {
    if (s_arcTimer) {
        lv_timer_del(s_arcTimer);
        s_arcTimer = nullptr;
    }
    if (s_breathObj) {
        lv_anim_del(s_breathObj, breathCb);
        s_breathObj = nullptr;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
    s_arc = nullptr;
    s_progressBar = nullptr;
    s_subLabel = nullptr;
    s_lastRenderedWritten = SIZE_MAX;
}

lv_obj_t *createUsbIcon(lv_obj_t *parent, uint32_t color) {
    constexpr int16_t SLEEVE_W = 18;
    constexpr int16_t SLEEVE_H = 24;
    constexpr int16_t CABLE_W = 4;
    constexpr int16_t CABLE_H = 6;
    constexpr int16_t CONTACT_W = 10;
    constexpr int16_t CONTACT_H = 4;
    constexpr int16_t CONTACT_TOP_PAD = 4;
    constexpr int16_t ICON_W = SLEEVE_W;
    constexpr int16_t ICON_H = SLEEVE_H + CABLE_H;

    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, ICON_W, ICON_H);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *sleeve = lv_obj_create(icon);
    lv_obj_set_size(sleeve, SLEEVE_W, SLEEVE_H);
    lv_obj_set_style_radius(sleeve, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sleeve, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sleeve, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sleeve, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sleeve, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sleeve, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sleeve, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *contact = lv_obj_create(sleeve);
    lv_obj_set_size(contact, CONTACT_W, CONTACT_H);
    lv_obj_set_style_radius(contact, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(contact, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(contact, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(contact, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(contact, 0, LV_PART_MAIN);
    lv_obj_clear_flag(contact, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(contact, LV_ALIGN_TOP_MID, 0, CONTACT_TOP_PAD);

    lv_obj_t *cable = lv_obj_create(icon);
    lv_obj_set_size(cable, CABLE_W, CABLE_H);
    lv_obj_set_style_radius(cable, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cable, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cable, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cable, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cable, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cable, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cable, LV_ALIGN_BOTTOM_MID, 0, 0);

    return icon;
}

} // namespace

namespace OtaOverlay {

void show(size_t totalBytes) {
    teardownOverlay();

    s_total.store(totalBytes, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);
    s_lastRenderedWritten = SIZE_MAX;

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);

    lv_obj_t *col = lv_obj_create(root);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, LV_PART_MAIN);

    static constexpr int16_t kArcSize = 56;
    lv_obj_t *arc = lv_arc_create(col);
    lv_obj_set_size(arc, kArcSize, kArcSize);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, kArcSpan);
    lv_arc_set_rotation(arc, 0);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    s_arcAngle = 0;
    s_arcTimer = lv_timer_create(arcTimerCb, kArcTickMs, arc);

    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, "Updating firmware");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);

    lv_obj_t *bar = lv_bar_create(col);
    lv_obj_set_size(bar, 200, 6);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

    lv_obj_t *sub = lv_label_create(col);
    lv_label_set_text(sub, "0 / 0 KB");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sub, FontManager::label(12), 0);

    lv_obj_t *usbIcon = createUsbIcon(col, 0xFF3030);
    s_breathObj = usbIcon;
    lv_anim_t breath;
    lv_anim_init(&breath);
    lv_anim_set_exec_cb(&breath, breathCb);
    lv_anim_set_var(&breath, usbIcon);
    lv_anim_set_values(&breath, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&breath, 900);
    lv_anim_set_playback_time(&breath, 900);
    lv_anim_set_repeat_count(&breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&breath);

    s_overlay = root;
    s_arc = arc;
    s_progressBar = bar;
    s_subLabel = sub;

    lv_refr_now(nullptr);
}

void setProgress(size_t writtenBytes) {
    s_written.store(writtenBytes, std::memory_order_relaxed);
}

void hide() {
    teardownOverlay();
    s_total.store(0, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);
}

bool isActive() {
    return s_overlay != nullptr;
}

namespace Detail {

void tick() {
    if (s_overlay == nullptr)
        return;
    const size_t written = s_written.load(std::memory_order_relaxed);
    if (written == s_lastRenderedWritten)
        return;
    s_lastRenderedWritten = written;
    const size_t total = s_total.load(std::memory_order_relaxed);
    if (s_progressBar && total > 0) {
        const int32_t pctMille = static_cast<int32_t>((written * 1000ULL) / total);
        lv_bar_set_value(s_progressBar, pctMille, LV_ANIM_OFF);
    }
    if (s_subLabel) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%u / %u KB", static_cast<unsigned>(written / 1024),
                 static_cast<unsigned>(s_total.load(std::memory_order_relaxed) / 1024));
        lv_label_set_text(s_subLabel, buf);
    }
}

} // namespace Detail

} // namespace OtaOverlay
