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

constexpr int16_t PANEL_H = 240;
constexpr int16_t PANEL_PAD = 6;
constexpr int16_t ROW_H = 18;

// Sized for XPT2046 resistive jitter (~10 px centroid spread) — paired with
// lv_obj_set_ext_click_area for an even larger hit rect.
constexpr int16_t CLOSE_BTN_SIZE = 36;
constexpr int16_t CLOSE_BTN_EXT_CLICK_PAD = 12;

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

// Wire format matches signals.json names.

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

lv_obj_t *s_panel = nullptr;
lv_obj_t *s_closeBtn = nullptr;
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
        // 12×12 (was 8×8) — at 8 px the rounded-corner mask was too tight for
        // LVGL's draw-mask resolver to render a visible circle, so the badge
        // looked like a small coloured square. 12 px is large enough that
        // LV_RADIUS_CIRCLE produces a clearly circular dot at the same row
        // height (ROW_H=18).
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

void onCloseReleased(lv_event_t * /*e*/) {
    // Listen on LV_EVENT_RELEASED (not LV_EVENT_CLICKED) because the panel
    // is LV_OBJ_FLAG_SCROLLABLE and resistive-touch jitter routinely starts
    // a scroll. From lv_indev.c (LVGL 8.3, line 973):
    //     /*Send CLICK if no scrolling*/
    //     if(scroll_obj == NULL) { … SHORT_CLICKED … CLICKED … }
    // — i.e. CLICKED and SHORT_CLICKED are BOTH suppressed once any scroll
    // begins. RELEASED is the only release event guaranteed to fire on the
    // currently-pressed object regardless of scroll state, and combined
    // with LV_OBJ_FLAG_PRESS_LOCK (which keeps the press anchored on this
    // button) it gives a reliable tap-to-close on a noisy touch panel.
    LOG_INFO("DIAG_DRAWER", "close release");
    close();
}

void onClosePressed(lv_event_t * /*e*/) {
    // Diagnostic shim — fires before any scroll arbitration so we can
    // distinguish two failure modes on device:
    //   1. "press" line absent  → touch is NOT reaching the button at all
    //      (hit-area, z-order, or coord/clip issue — H5)
    //   2. "press" line present but "release" line absent → press arrived
    //      but RELEASED never reached the button (PRESS_LOCK is unset or
    //      the indev re-resolved act_obj — H1/H3)
    // Leave this log in until a user-confirmed close cycle is observed on
    // the hardware build; then it can be removed in a follow-up.
    LOG_INFO("DIAG_DRAWER", "close press");
}

void onVerticalSwipe(lv_dir_t dir) {
    // Registered with GestureController. Swipe-up anywhere opens the
    // drawer; swipe-down inside the open drawer closes it (the panel
    // already has its own swipe-down handler for that case, but routing
    // both through here keeps the code obvious for a future reader).
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
    // FLAG_SCROLLABLE is default-on for `lv_obj_create`, but other code in
    // PR #952 cleared it on row containers via `applyRowReset`; keep it
    // explicit here so a future copy-paste from the row helper doesn't
    // disable it on the panel itself. Without this flag the section list
    // (flags + scalars + errors) overflows past PANEL_H and the user can't
    // reach the bottom rows.
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    // Swipe down anywhere on the panel collapses it back. Tapping the
    // handle (which is hidden while the panel is open) is not available.
    lv_obj_add_event_cb(s_panel, onPanelGesture, LV_EVENT_GESTURE, nullptr);

    buildFlagsSection(s_panel);
    buildScalarsSection(s_panel);
    buildErrorsSection(s_panel);

    // -------- Close button -------------------------------------------------
    // Child of s_panel (not lv_layer_top()) so the panel intercepts every
    // touch in its area and the topbar (sibling on lv_layer_top, lower in z)
    // can never receive a click while the drawer is open. Earlier the close
    // btn lived on lv_layer_top at TOP_RIGHT and shared coordinates with the
    // top bar's day/night toggle — a slightly off-target tap would close the
    // drawer AND flip the theme. FLOATING keeps the btn out of the panel's
    // flex-column layout so it doesn't stack between sections.
    s_closeBtn = lv_btn_create(s_panel);
    lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_FLOATING);
    // PRESS_LOCK keeps the press anchored to the button even when the touch
    // coordinate slides a few px (XPT2046 resistive panels jitter ±5-10 px).
    // Without it `lv_indev` re-resolves `indev_obj_act` each tick, the panel
    // wins because it's bigger, scrolling starts, and the eventual release
    // gets routed to s_panel — so LV_EVENT_CLICKED on the button never fires.
    // See lv_indev.c:837 (LVGL 8.3) for the press-lock branch.
    lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_PRESS_LOCK);
    // SCROLL_ON_FOCUS would scroll s_panel to bring the focused close-btn
    // into view, racing the touch resolution. The btn is always at the top
    // edge so this scroll is never useful; disable it explicitly.
    lv_obj_clear_flag(s_closeBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_size(s_closeBtn, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    // Extra hit area beyond the visible 36×36 — gives the user a ~60×60 px
    // tap target without the visual weight of a 60 px button on a 320 px
    // screen. `lv_obj_set_ext_click_area` grows the click rectangle on all
    // sides by the same amount.
    lv_obj_set_ext_click_area(s_closeBtn, CLOSE_BTN_EXT_CLICK_PAD);
    lv_obj_align(s_closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(s_closeBtn, lv_color_hex(COL_HANDLE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_closeBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_closeBtn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_closeBtn, lv_color_hex(COL_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(s_closeBtn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_closeBtn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_closeBtn, onCloseReleased, LV_EVENT_RELEASED, nullptr);
    // Diagnostic — see onClosePressed comment. Lets the user disambiguate
    // hit-test failure vs. event-routing failure on hardware.
    lv_obj_add_event_cb(s_closeBtn, onClosePressed, LV_EVENT_PRESSED, nullptr);

    lv_obj_t *closeLabel = lv_label_create(s_closeBtn);
    // Plain "X" — LV_SYMBOL_CLOSE (U+F00D, FontAwesome PUA) is not in our
    // Orbitron font and would render as a placeholder + flood LVGL warnings.
    lv_label_set_text(closeLabel, "X");
    lv_obj_set_style_text_font(closeLabel, FONT_SM(), 0);
    lv_obj_set_style_text_color(closeLabel, lv_color_hex(COL_VALUE), 0);
    lv_obj_center(closeLabel);

    s_initDone = true;
    // Diagnostic stamp — bumped each time this PR's bug-batch lands so the
    // user can confirm at boot that the device is actually running the new
    // build (vs. a stale upload). Grep the serial log for "DIAG_DRAWER".
    LOG_INFO("DIAG_DRAWER",
             "init done — close-btn v3: press-lock + ext-click + RELEASED + press/release logs");
}

void open() {
    if (!s_panel)
        return;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    // Top bar lives on lv_layer_top too and was init'd AFTER us by
    // PageManager::init, so it sits z-above the drawer panel by default —
    // visually covering the close X and making the drawer untappable.
    // Push the panel (and its FLOATING close-btn child) to the foreground
    // on every open so the topbar is hidden behind the panel as intended.
    lv_obj_move_foreground(s_panel);
    // Close btn is a child of s_panel — hidden flag on the parent already
    // propagates, no separate show/hide call needed.
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
    s_open = false;
}

void update() {
    if (!s_panel || !s_open)
        return;

    // Reaffirm z-top every tick while the drawer is open. open() alone is not
    // enough — any subsequent z-touching operation on a same-layer sibling
    // (top bar item rebuild, settings panel show/hide, …) can leave the panel
    // below the top bar visually, making the X tap target unreachable.
    lv_obj_move_foreground(s_panel);

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
