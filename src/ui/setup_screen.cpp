#include "setup_screen.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/font_manager.h"

#include <lvgl.h>
#include <stdio.h>

namespace SetupScreen {

namespace {

void animBreath(void *obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
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

void show() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logoRow = lv_obj_create(scr);
    lv_obj_set_size(logoRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(logoRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logoRow, 0, 0);
    lv_obj_set_style_pad_all(logoRow, 0, 0);
    lv_obj_clear_flag(logoRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(logoRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(logoRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(logoRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(logoRow, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *logoCan = lv_label_create(logoRow);
    lv_label_set_text(logoCan, "CAN");
    lv_obj_set_style_text_font(logoCan, FontManager::primary(32), 0);
    lv_obj_set_style_text_color(logoCan, lv_color_hex(0x9A9A9A), 0);

    lv_obj_t *logoShift = lv_label_create(logoRow);
    lv_label_set_text(logoShift, "Shift");
    lv_obj_set_style_text_font(logoShift, FontManager::primary(32), 0);
    lv_obj_set_style_text_color(logoShift, lv_color_hex(0xFF4444), 0);

    lv_obj_t *logo = logoRow;

    char verBuf[16];
    snprintf(verBuf, sizeof(verBuf), "v" APP_VERSION_STR);
    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, verBuf);
    lv_obj_set_style_text_font(ver, FontManager::label(12), 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x444444), 0);
    lv_obj_align_to(ver, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, 200, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ready to configure");
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t *instr = lv_label_create(scr);
    lv_label_set_text(instr, "Open CANShift Studio and connect\nthis device via USB.");
    lv_obj_set_style_text_font(instr, FontManager::label(12), 0);
    lv_obj_set_style_text_color(instr, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(instr, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(instr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(instr, LV_HOR_RES - 40);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 26);

    lv_obj_t *usbIcon = createUsbIcon(scr, 0xFF3030);
    lv_obj_align(usbIcon, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, animBreath);
    lv_anim_set_var(&a, usbIcon);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, 900);
    lv_anim_set_playback_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_scr_load(scr);
    LOG_INFO("UI", "Setup screen shown — waiting for Studio connection");
}

} // namespace SetupScreen
