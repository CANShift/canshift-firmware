#include "diag_drawer.h"

#include "can/signal_map.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"
#include "ui/font_manager.h"
#include "ui/gesture_controller.h"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace DiagDrawer {

namespace {

constexpr int16_t PANEL_H = 220;
constexpr int16_t PANEL_PAD = 6;
constexpr int16_t ROW_H = 18;

constexpr int16_t CLOSE_BTN_SIZE = 36;
constexpr int16_t CLOSE_BTN_EXT_CLICK_PAD = 12;

constexpr int16_t TAP_OUTSIDE_H = 20;

constexpr int16_t HANDLE_W = 48;
constexpr int16_t HANDLE_H = 5;
constexpr int16_t HANDLE_BOTTOM_MARGIN = 3;
constexpr int16_t HANDLE_EXT_CLICK_PAD = 12;

constexpr int16_t FLAGS_COUNT = 5;
constexpr int16_t SCALARS_COUNT = 4;
constexpr int16_t ERRORS_MAX_ROWS = 4;

constexpr uint32_t COL_HANDLE_BG = 0x1A1A22;
constexpr uint32_t COL_HANDLE_TXT = 0x666688;
constexpr uint32_t COL_PANEL_BG = 0x0C0C12;
constexpr uint32_t COL_PANEL_BORDER = 0x222230;
constexpr uint32_t COL_SECTION_HDR = 0x6677AA;
constexpr uint32_t COL_LABEL = 0xAAAAAA;
constexpr uint32_t COL_VALUE = 0xDDDDDD;
constexpr uint32_t COL_FLAG_ACTIVE = 0xFF4444;
constexpr uint32_t COL_FLAG_INACTIVE = 0x444444;
constexpr uint32_t COL_ERROR_CODE = 0xCC4444;
constexpr uint32_t COL_ERROR_MSG = 0xDDAAAA;

struct FlagRow {
    const char *label;
    SignalId signalId;
};

const FlagRow s_flags[FLAGS_COUNT] = {
    {"MIL", SignalIds::FLAG_MIL},
    {"Launch", SignalIds::FLAG_LAUNCH_CTRL},
    {"Flat shift", SignalIds::FLAG_FLAT_SHIFT},
    {"Anti-lag", SignalIds::FLAG_ANTI_LAG},
    {"TC cut", SignalIds::FLAG_TRACTION_CUT},
};

struct ScalarRow {
    const char *label;
    SignalId signalId;
    const char *unit;
    uint8_t decimals;
};

const ScalarRow s_scalars[SCALARS_COUNT] = {
    {"RPM", SignalIds::RPM, "", 0},
    {"TPS", SignalIds::THROTTLE_POS, "%", 0},
    {"Coolant", SignalIds::COOLANT_TEMP_C, "°C", 0},
    {"Battery", SignalIds::BATTERY_VOLTS, "V", 1},
};

lv_obj_t *s_panel = nullptr;
lv_obj_t *s_closeBtn = nullptr;
lv_obj_t *s_tapOutsideZone = nullptr;
lv_obj_t *s_handle = nullptr;
lv_obj_t *s_flagBadges[FLAGS_COUNT] = {nullptr};
lv_obj_t *s_scalarValues[SCALARS_COUNT] = {nullptr};
lv_obj_t *s_errorRows[ERRORS_MAX_ROWS] = {nullptr};
lv_obj_t *s_errorCodes[ERRORS_MAX_ROWS] = {nullptr};
lv_obj_t *s_errorMsgs[ERRORS_MAX_ROWS] = {nullptr};
lv_obj_t *s_errorEmptyLabel = nullptr;

bool s_open = false;
uint32_t s_lastErrorVersion = UINT32_MAX;
bool s_initDone = false;

const lv_font_t *FONT_SM() {
    return FontManager::label(12);
}

const lv_font_t *FONT_HDR() {
    return FontManager::label(12);
}

void applyRowReset(lv_obj_t *row) {
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *sectionHeader(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_HDR(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_SECTION_HDR), 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);
    lv_obj_set_style_pad_top(lbl, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(lbl, 2, LV_PART_MAIN);
    return lbl;
}

void buildFlagsSection(lv_obj_t *parent) {
    sectionHeader(parent, "ECU FLAGS");
    for (uint8_t i = 0; i < FLAGS_COUNT; ++i) {
        lv_obj_t *row = lv_obj_create(parent);
        applyRowReset(row);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);

        lv_obj_t *badge = lv_obj_create(row);

        lv_obj_set_size(badge, 12, 12);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(badge, lv_color_hex(COL_FLAG_INACTIVE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(badge, 0, LV_PART_MAIN);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_flagBadges[i] = badge;

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, s_flags[i].label);
        lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    }
}

void buildScalarsSection(lv_obj_t *parent) {
    sectionHeader(parent, "STATUS");
    for (uint8_t r = 0; r < 2; ++r) {
        lv_obj_t *row = lv_obj_create(parent);
        applyRowReset(row);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        for (uint8_t c = 0; c < 2; ++c) {
            const uint8_t idx = static_cast<uint8_t>(r * 2 + c);
            lv_obj_t *cell = lv_obj_create(row);
            applyRowReset(cell);
            lv_obj_set_size(cell, LV_PCT(48), ROW_H);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);

            lv_obj_t *lbl = lv_label_create(cell);
            lv_label_set_text(lbl, s_scalars[idx].label);
            lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);

            lv_obj_t *val = lv_label_create(cell);
            lv_label_set_text(val, "--");
            lv_obj_set_style_text_font(val, FONT_SM(), 0);
            lv_obj_set_style_text_color(val, lv_color_hex(COL_VALUE), 0);
            s_scalarValues[idx] = val;
        }
    }
}

void buildErrorsSection(lv_obj_t *parent) {
    sectionHeader(parent, "FIRMWARE ERRORS");
    for (uint8_t i = 0; i < ERRORS_MAX_ROWS; ++i) {
        lv_obj_t *row = lv_obj_create(parent);
        applyRowReset(row);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);

        lv_obj_t *code = lv_label_create(row);
        lv_label_set_text(code, "");
        lv_obj_set_style_text_font(code, FONT_SM(), 0);
        lv_obj_set_style_text_color(code, lv_color_hex(COL_ERROR_CODE), 0);
        s_errorCodes[i] = code;

        lv_obj_t *msg = lv_label_create(row);
        lv_label_set_text(msg, "");
        lv_label_set_long_mode(msg, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(msg, FONT_SM(), 0);
        lv_obj_set_style_text_color(msg, lv_color_hex(COL_ERROR_MSG), 0);
        lv_obj_set_flex_grow(msg, 1);
        s_errorMsgs[i] = msg;

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        s_errorRows[i] = row;
    }
    lv_obj_t *empty = lv_label_create(parent);
    lv_label_set_text(empty, "no firmware errors");
    lv_obj_set_style_text_font(empty, FONT_SM(), 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x555555), 0);
    s_errorEmptyLabel = empty;
}

const char *errorSrcLabel(ErrorSource src) {
    switch (src) {
        case ERROR_SRC_CAN:
            return "CAN";
        case ERROR_SRC_CONFIG:
            return "CFG";
        case ERROR_SRC_SYSTEM:
            return "SYS";
    }
    return "?";
}

void onCloseReleased(lv_event_t *) {

    LOG_INFO("DIAG_DRAWER", "close release");
    close();
}

void onClosePressed(lv_event_t *) {
    LOG_INFO("DIAG_DRAWER", "close press");
}

void onTapOutside(lv_event_t *) {
    if (s_open)
        close();
}

void onVerticalSwipe(lv_dir_t dir) {
    if (dir == LV_DIR_TOP && !s_open) {
        open();
    }
}

void onPanelGesture(lv_event_t *e) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        close();
    }
    (void)e;
}

void onHandleClicked(lv_event_t *) {
    if (!s_open)
        open();
}

void onHandleGesture(lv_event_t *) {
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP && !s_open) {
        lv_indev_wait_release(lv_indev_get_act());
        open();
    }
}

} // namespace

void init() {
    if (s_initDone)
        return;

    GestureController::setVerticalSwipeHandler(onVerticalSwipe);

    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_panel, LV_HOR_RES, PANEL_H);
    lv_obj_align(s_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(COL_PANEL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(COL_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, PANEL_PAD, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_panel, 2, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_panel, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_panel, onPanelGesture, LV_EVENT_GESTURE, nullptr);

    buildFlagsSection(s_panel);
    buildScalarsSection(s_panel);
    buildErrorsSection(s_panel);

    s_closeBtn = lv_btn_create(s_panel);
    lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_FLOATING);

    lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(s_closeBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_size(s_closeBtn, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    lv_obj_set_ext_click_area(s_closeBtn, CLOSE_BTN_EXT_CLICK_PAD);
    lv_obj_align(s_closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(s_closeBtn, lv_color_hex(COL_HANDLE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_closeBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_closeBtn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_closeBtn, lv_color_hex(COL_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(s_closeBtn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_closeBtn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_closeBtn, onCloseReleased, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_closeBtn, onClosePressed, LV_EVENT_PRESSED, nullptr);

    lv_obj_t *closeLabel = lv_label_create(s_closeBtn);

    lv_label_set_text(closeLabel, "X");
    lv_obj_set_style_text_font(closeLabel, FONT_SM(), 0);
    lv_obj_set_style_text_color(closeLabel, lv_color_hex(COL_VALUE), 0);
    lv_obj_center(closeLabel);

    s_tapOutsideZone = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_tapOutsideZone, LV_HOR_RES, TAP_OUTSIDE_H);
    lv_obj_align(s_tapOutsideZone, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_tapOutsideZone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tapOutsideZone, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tapOutsideZone, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_tapOutsideZone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_tapOutsideZone, onTapOutside, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_tapOutsideZone, LV_OBJ_FLAG_HIDDEN);

    s_handle = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_handle, HANDLE_W, HANDLE_H);
    lv_obj_align(s_handle, LV_ALIGN_BOTTOM_MID, 0, -HANDLE_BOTTOM_MARGIN);
    lv_obj_set_style_bg_color(s_handle, lv_color_hex(COL_HANDLE_TXT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_handle, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_handle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_handle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_handle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(s_handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_handle, HANDLE_EXT_CLICK_PAD);
    lv_obj_add_event_cb(s_handle, onHandleClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_handle, onHandleGesture, LV_EVENT_GESTURE, nullptr);

    s_initDone = true;
    LOG_INFO("DIAG_DRAWER", "init done — handle + tap-outside + swipe + X");
}

void open() {
    if (!s_panel)
        return;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(s_panel);
    if (s_tapOutsideZone) {
        lv_obj_clear_flag(s_tapOutsideZone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_tapOutsideZone);
    }
    if (s_handle)
        lv_obj_add_flag(s_handle, LV_OBJ_FLAG_HIDDEN);
    s_open = true;

    s_lastErrorVersion = UINT32_MAX;
    update();
}

void close() {
    if (!s_panel)
        return;
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_tapOutsideZone)
        lv_obj_add_flag(s_tapOutsideZone, LV_OBJ_FLAG_HIDDEN);
    if (s_handle)
        lv_obj_clear_flag(s_handle, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
}

void update() {
    if (!s_panel || !s_open)
        return;

    lv_obj_move_foreground(s_panel);

    for (uint8_t i = 0; i < FLAGS_COUNT; ++i) {
        if (!s_flagBadges[i])
            continue;
        const SignalId sid = s_flags[i].signalId;
        const bool active = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid) &&
                            SignalStore::read(sid, 0.0f) != 0.0f;
        const uint32_t col = active ? COL_FLAG_ACTIVE : COL_FLAG_INACTIVE;
        lv_obj_set_style_bg_color(s_flagBadges[i], lv_color_hex(col), LV_PART_MAIN);
    }

    for (uint8_t i = 0; i < SCALARS_COUNT; ++i) {
        if (!s_scalarValues[i])
            continue;
        const SignalId sid = s_scalars[i].signalId;
        char buf[24];
        if (sid >= SignalIds::SIGNAL_COUNT || !SignalStore::isValid(sid)) {
            snprintf(buf, sizeof(buf), "--");
        } else {
            const float v = SignalStore::read(sid, 0.0f);
            if (s_scalars[i].decimals == 0) {
                snprintf(buf, sizeof(buf), "%d%s", static_cast<int>(v), s_scalars[i].unit);
            } else {
                snprintf(buf, sizeof(buf), "%.1f%s", static_cast<double>(v), s_scalars[i].unit);
            }
        }
        lv_label_set_text(s_scalarValues[i], buf);
    }

    const uint32_t errVersion = ErrorStore::getVersion();
    if (errVersion != s_lastErrorVersion) {
        s_lastErrorVersion = errVersion;

        FwError errors[ERRORS_MAX_ROWS];
        uint8_t fetched = 0;
        ErrorStore::getAll(errors, &fetched, ERRORS_MAX_ROWS);

        for (uint8_t i = 0; i < ERRORS_MAX_ROWS; ++i) {
            if (!s_errorRows[i])
                continue;
            if (i < fetched) {
                char codeBuf[24];
                snprintf(codeBuf, sizeof(codeBuf), "%s:%s", errorSrcLabel(errors[i].source),
                         errors[i].code);
                lv_label_set_text(s_errorCodes[i], codeBuf);
                lv_label_set_text(s_errorMsgs[i], errors[i].message);
                lv_obj_clear_flag(s_errorRows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_errorRows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (s_errorEmptyLabel) {
            if (fetched == 0) {
                lv_obj_clear_flag(s_errorEmptyLabel, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_errorEmptyLabel, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

} // namespace DiagDrawer
