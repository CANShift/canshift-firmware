#include "settings_page_internal.h"

#include "ui/font_manager.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdint.h>
#include <stdlib.h>

namespace SettingsPageInternal {

namespace {

inline const lv_font_t *FONT_LG() {
    return FontManager::label(12);
}
inline const lv_font_t *FONT_SM() {
    return FontManager::label(12);
}

lv_obj_t *makeSlider(lv_obj_t *parent, int32_t vmin, int32_t vmax, int32_t initial,
                     lv_event_cb_t cb) {
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, lv_obj_get_width(parent));
    lv_slider_set_range(slider, vmin, vmax);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);

    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return slider;
}

lv_obj_t *makeSegButton(lv_obj_t *parent, const char *label, bool active, lv_event_cb_t cb,
                        void *userData) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? CLR_BTN_ACT : CLR_BTN_BG), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? CLR_ACCENT : CLR_BTN_BDR),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    return btn;
}

lv_obj_t *makeFullButton(lv_obj_t *parent, const char *label, uint32_t bgColor, uint32_t bdrColor,
                         uint32_t textColor, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(bdrColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 3, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(textColor), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

} // namespace

void computePanelGeometry(int16_t yOffset, int16_t height) {

    s_openY = yOffset;
    s_panelHeight = height;
    s_closedY = static_cast<int16_t>(yOffset - height);
}

lv_obj_t *createPanel(int16_t yOffset, int16_t panelW, int16_t height) {
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(panel, 0, yOffset);
    lv_obj_set_size(panel, panelW, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, 8, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

void buildHeader(int16_t &y) {
    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "SCREEN SETTINGS");
    lv_obj_set_style_text_font(title, FONT_LG(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_pos(title, PAD_H, y);
    y += HEADER_H;
}

void buildBrightnessRow(int16_t &y, int16_t rowW) {
    lv_obj_t *row = lv_obj_create(s_panel);
    lv_obj_set_pos(row, PAD_H, y);
    lv_obj_set_size(row, rowW, LABEL_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "BRIGHTNESS");
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_brValue = lv_label_create(row);
    lv_obj_set_style_text_font(s_brValue, FONT_SM(), 0);
    lv_obj_set_style_text_color(s_brValue, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(s_brValue, LV_ALIGN_RIGHT_MID, 0, 0);
    updateBrValue();

    y += LABEL_H + GAP_INNER;

    s_brSlider = makeSlider(s_panel, 10, 100, s_brightness, onBrightnessChanged);
    lv_obj_set_pos(s_brSlider, PAD_H, y);
    lv_obj_set_size(s_brSlider, rowW, SLIDER_H);
    y += SLIDER_H;
}

void buildBleRow(int16_t &y, int16_t rowW) {
    lv_obj_t *lbl = lv_label_create(s_panel);
    lv_label_set_text(lbl, "MOBILE PAIRING");
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_pos(lbl, PAD_H, y);
    y += LABEL_H + GAP_INNER;

    const int16_t gap = 4;
    const int16_t btnW = (rowW - gap) / 2;
    const char *const bleLabels[2] = {"ON", "OFF"};
    for (uint8_t i = 0; i < 2; ++i) {
        bool active = (i == 0) ? s_bleEnabled : !s_bleEnabled;
        s_bleBtns[i] = makeSegButton(s_panel, bleLabels[i], active, onBleBtn,
                                     reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_pos(s_bleBtns[i], PAD_H + i * (btnW + gap), y);
        lv_obj_set_size(s_bleBtns[i], btnW, BTN_H);
    }
    y += BTN_H;
}

void buildCalibrateTouchRow(int16_t &y, int16_t rowW) {
    lv_obj_t *btn = makeFullButton(s_panel, "CALIBRATE TOUCH", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED,
                                   onCalibrateTouch);
    lv_obj_set_pos(btn, PAD_H, y);
    lv_obj_set_size(btn, rowW, BTN_H);
    y += BTN_H;
}

void buildResetTouchCalRow(int16_t &y, int16_t rowW) {
    lv_obj_t *btn = makeFullButton(s_panel, "RESET TOUCH CAL", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED,
                                   onResetTouchCal);
    lv_obj_set_pos(btn, PAD_H, y);
    lv_obj_set_size(btn, rowW, BTN_H);
    y += BTN_H;
}

void buildActionsRow(int16_t y, int16_t rowW) {
    (void)y;
    (void)rowW;
}

void animSetY(void *obj, int32_t v) {
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
}

void onSnapOpenDone(lv_anim_t *) {
    if (!s_panel)
        return;
    lv_obj_set_y(s_panel, s_openY);
    if (!s_open) {
        s_open = true;
        s_lastOpenMs = millis();
        LOG_DEBUG("Settings", "Settings page opened (snap)");
    }
}

void onSnapClosedDone(lv_anim_t *) {
    if (!s_panel)
        return;
    lv_obj_set_y(s_panel, s_closedY);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_open) {
        s_open = false;
        LOG_DEBUG("Settings", "Settings page closed (snap)");
    }
}

void runSnap(int16_t targetY, lv_anim_ready_cb_t doneCb) {
    if (!s_panel)
        return;
    int16_t fromY = lv_obj_get_y(s_panel);

    lv_anim_del(s_panel, animSetY);

    uint32_t deltaPx = static_cast<uint32_t>(abs(targetY - fromY));
    uint32_t panelH = static_cast<uint32_t>(s_panelHeight > 0 ? s_panelHeight : 1);
    uint32_t durationMs = (SNAP_ANIM_MS * deltaPx) / panelH;
    if (durationMs < 60)
        durationMs = 60;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, animSetY);
    lv_anim_set_values(&a, fromY, targetY);
    lv_anim_set_time(&a, durationMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, doneCb);
    lv_anim_start(&a);
}

} // namespace SettingsPageInternal
