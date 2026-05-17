// diag_drawer.cpp — Bottom diagnostic panel (#635).
//
// Owns three sections rendered as static rows so the UI hot path never
// touches the heap: ECU fault flags (driven by `flag_*` signals), a 2×2 ECU
// scalar grid (RPM, TPS, coolant, battery), and a firmware error list
// forwarded from `ErrorStore`. Refreshes are version-gated where possible
// (errors) or skipped when hidden (flags/scalars).

#include "diag_drawer.h"

#include "can/signal_map.h"
#include "diag/error_store.h"
#include "runtime/signal_store.h"
#include "ui/font_manager.h"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace DiagDrawer {

namespace {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// Handle = the always-visible tab the user taps to open the drawer. Kept
// narrow vertically so it does not eat into widget real estate, but wide
// enough to be a comfortable tap target on the resistive XPT2046 touch.
constexpr int16_t HANDLE_H = 14;

// Drawer panel target dimensions — matches the issue's "~180 px" guidance.
constexpr int16_t PANEL_H = 180;
constexpr int16_t PANEL_PAD = 6;
constexpr int16_t ROW_H = 18;

// Section heights computed from row counts so they stay in sync with the
// row arrays below.
constexpr int16_t FLAGS_COUNT = 5;
constexpr int16_t SCALARS_COUNT = 4;
constexpr int16_t ERRORS_MAX_ROWS = 4; // Soft cap — panel scrolls beyond it.

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Flag + scalar declarations — wire format matches signals.json names.
// ---------------------------------------------------------------------------

struct FlagRow {
    const char *label;
    SignalId signalId;
};

// Mirrors the FRAME 0x374 status bitmask laid out in signals.json. Adding a
// new flag = one row here + the matching signal in signals.json + a SignalId
// constant in signal_map.h.
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

// ---------------------------------------------------------------------------
// LVGL handles
// ---------------------------------------------------------------------------

lv_obj_t *s_handle = nullptr;
lv_obj_t *s_handleLabel = nullptr;
lv_obj_t *s_panel = nullptr;
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

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

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
        lv_obj_set_size(badge, 8, 8);
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
    // 2×2 grid — two rows of two scalars each. Each cell is a label+value
    // pair laid out as `LABEL: VALUE unit`.
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
            lv_label_set_text(val, "—");
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
    // Friendly empty-state label so the section never looks broken.
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

void onHandleClicked(lv_event_t * /*e*/) {
    s_open ? close() : open();
}

void onPanelGesture(lv_event_t *e) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        close();
    }
    (void)e;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void init() {
    if (s_initDone)
        return;

    // -------- Handle -------------------------------------------------------
    s_handle = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_handle, LV_HOR_RES, HANDLE_H);
    lv_obj_align(s_handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_handle, lv_color_hex(COL_HANDLE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_handle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_handle, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_handle, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_handle, lv_color_hex(COL_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_handle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_handle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_handle, onHandleClicked, LV_EVENT_CLICKED, nullptr);

    s_handleLabel = lv_label_create(s_handle);
    lv_label_set_text(s_handleLabel, "▴ DIAG");
    lv_obj_set_style_text_font(s_handleLabel, FONT_SM(), 0);
    lv_obj_set_style_text_color(s_handleLabel, lv_color_hex(COL_HANDLE_TXT), 0);
    lv_obj_center(s_handleLabel);

    // -------- Panel --------------------------------------------------------
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
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    // Swipe down anywhere on the panel collapses it back. Tapping the
    // handle (which is hidden while the panel is open) is not available.
    lv_obj_add_event_cb(s_panel, onPanelGesture, LV_EVENT_GESTURE, nullptr);

    buildFlagsSection(s_panel);
    buildScalarsSection(s_panel);
    buildErrorsSection(s_panel);

    s_initDone = true;
}

void open() {
    if (!s_panel)
        return;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    // Hide the handle while open so it does not overlap the panel border.
    if (s_handle)
        lv_obj_add_flag(s_handle, LV_OBJ_FLAG_HIDDEN);
    s_open = true;
    // Force the next update() to refresh the error rows even if the version
    // hasn't moved since the last open.
    s_lastErrorVersion = UINT32_MAX;
    update();
}

void close() {
    if (!s_panel)
        return;
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_handle)
        lv_obj_clear_flag(s_handle, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
}

void update() {
    if (!s_panel || !s_open)
        return;

    // Flags
    for (uint8_t i = 0; i < FLAGS_COUNT; ++i) {
        if (!s_flagBadges[i])
            continue;
        const SignalId sid = s_flags[i].signalId;
        const bool active = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid) &&
                            SignalStore::read(sid, 0.0f) != 0.0f;
        const uint32_t col = active ? COL_FLAG_ACTIVE : COL_FLAG_INACTIVE;
        lv_obj_set_style_bg_color(s_flagBadges[i], lv_color_hex(col), LV_PART_MAIN);
    }

    // Scalars
    for (uint8_t i = 0; i < SCALARS_COUNT; ++i) {
        if (!s_scalarValues[i])
            continue;
        const SignalId sid = s_scalars[i].signalId;
        char buf[24];
        if (sid >= SignalIds::SIGNAL_COUNT || !SignalStore::isValid(sid)) {
            snprintf(buf, sizeof(buf), "—");
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

    // Errors — only re-render when ErrorStore's version moved.
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
