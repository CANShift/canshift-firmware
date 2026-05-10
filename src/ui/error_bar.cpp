// error_bar.cpp — Persistent bottom error overlay (lv_layer_top).

#include "error_bar.h"
#include "ui/font_manager.h"
#include "diag/error_store.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static constexpr int16_t BAR_H = 20;   // Collapsed height (px)
static constexpr int16_t ROW_H = 20;   // Height of each expanded row
static constexpr uint8_t MAX_ROWS = 3; // Max errors shown when expanded

static constexpr uint32_t COL_BG = 0x160808;     // Very dark red background
static constexpr uint32_t COL_BORDER = 0xCC3333; // Left accent
static constexpr uint32_t COL_CODE = 0xCC4444;   // Source:code text
static constexpr uint32_t COL_MSG = 0xDDAAAA;    // Message text
static constexpr uint32_t COL_DIM = 0x664444;    // Dimmed / dismiss button

// Lazy accessor — file-scope static initializers run before FontManager::init()
// in boot_sequence, so caching the pointer at static-init time would freeze it
// to LV_FONT_DEFAULT instead of the actual size 12 loaded from SPIFFS.
static inline const lv_font_t *FONT() {
    return FontManager::label(12);
}

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static lv_obj_t *s_container = nullptr;  // Outer container (the bar itself)
static lv_obj_t *s_headerRow = nullptr;  // Always-visible first row
static lv_obj_t *s_codeLabel = nullptr;  // "⚠ SRC:CODE"
static lv_obj_t *s_msgLabel = nullptr;   // Truncated message
static lv_obj_t *s_countLabel = nullptr; // "+N more" badge
static lv_obj_t *s_dismissBtn = nullptr; // × button on header row

static lv_obj_t *s_detailPanel = nullptr;            // Expanded panel
static lv_obj_t *s_detailRows[MAX_ROWS] = {nullptr}; // Row containers
static lv_obj_t *s_detailCode[MAX_ROWS] = {nullptr}; // SRC:CODE per row
static lv_obj_t *s_detailMsg[MAX_ROWS] = {nullptr};  // Message per row
static lv_obj_t *s_detailDism[MAX_ROWS] = {nullptr}; // × per row

static bool s_expanded = false;
static uint32_t s_lastVersion = UINT32_MAX; // Force first render

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char *srcLabel(ErrorSource src) {
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

static lv_obj_t *makeLabel(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, FONT(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

static lv_obj_t *makeDismissBtn(lv_obj_t *parent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BAR_H, BAR_H);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl, FONT(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
    lv_obj_center(lbl);
    return btn;
}

static void applyRowStyle(lv_obj_t *row) {
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// Expand / collapse
// ---------------------------------------------------------------------------

static void setExpanded(bool expand) {
    s_expanded = expand;
    if (!s_detailPanel)
        return;
    if (expand) {
        lv_obj_clear_flag(s_detailPanel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_detailPanel, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ErrorBar::init() {
    // Outer container — flush to the bottom of lv_layer_top()
    s_container = lv_obj_create(lv_layer_top());
    lv_obj_set_width(s_container, LV_HOR_RES);
    lv_obj_set_height(s_container, BAR_H); // Expanded later in update()
    lv_obj_align(s_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_container, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_container, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_container, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_container, lv_color_hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_container, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN); // Hidden until first error

    // Click on container = toggle expanded
    lv_obj_add_event_cb(
        s_container, [](lv_event_t * /*e*/) { setExpanded(!s_expanded); }, LV_EVENT_CLICKED,
        nullptr);

    // ---------------------------------------------------------------------------
    // Header row — always visible when bar is shown
    // ---------------------------------------------------------------------------
    s_headerRow = lv_obj_create(s_container);
    lv_obj_set_size(s_headerRow, LV_PCT(100), BAR_H);
    applyRowStyle(s_headerRow);
    lv_obj_set_style_pad_left(s_headerRow, 4, LV_PART_MAIN);
    // Stop header row clicks from reaching the container's expand toggle
    lv_obj_clear_flag(s_headerRow, LV_OBJ_FLAG_CLICKABLE);

    s_codeLabel = makeLabel(s_headerRow, COL_CODE);
    lv_label_set_text(s_codeLabel, "");
    lv_obj_set_width(s_codeLabel, LV_SIZE_CONTENT);

    s_msgLabel = makeLabel(s_headerRow, COL_MSG);
    lv_label_set_text(s_msgLabel, "");
    lv_label_set_long_mode(s_msgLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_msgLabel, 1);

    s_countLabel = makeLabel(s_headerRow, COL_CODE);
    lv_label_set_text(s_countLabel, "");
    lv_obj_set_width(s_countLabel, LV_SIZE_CONTENT);

    s_dismissBtn = makeDismissBtn(s_headerRow);
    // Dismiss button: dismiss latest and stop propagation (don't toggle expand)
    lv_obj_add_event_cb(
        s_dismissBtn,
        [](lv_event_t *e) {
            lv_event_stop_bubbling(e);
            ErrorStore::dismissLatest();
            if (ErrorStore::getCount() == 0) {
                setExpanded(false);
            }
        },
        LV_EVENT_CLICKED, nullptr);

    // ---------------------------------------------------------------------------
    // Detail panel — shown when expanded
    // ---------------------------------------------------------------------------
    s_detailPanel = lv_obj_create(s_container);
    lv_obj_set_width(s_detailPanel, LV_PCT(100));
    lv_obj_set_height(s_detailPanel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_detailPanel, lv_color_hex(0x100505), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_detailPanel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_detailPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_detailPanel, lv_color_hex(0x2A1010), LV_PART_MAIN);
    lv_obj_set_style_border_side(s_detailPanel, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_detailPanel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_detailPanel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_detailPanel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_detailPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_detailPanel, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        s_detailRows[i] = lv_obj_create(s_detailPanel);
        lv_obj_set_size(s_detailRows[i], LV_PCT(100), ROW_H);
        applyRowStyle(s_detailRows[i]);
        lv_obj_set_style_pad_left(s_detailRows[i], 6, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_detailRows[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_detailRows[i], lv_color_hex(0x2A1010), LV_PART_MAIN);
        lv_obj_set_style_border_side(s_detailRows[i], LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_clear_flag(s_detailRows[i], LV_OBJ_FLAG_CLICKABLE);

        s_detailCode[i] = makeLabel(s_detailRows[i], COL_CODE);
        lv_label_set_text(s_detailCode[i], "");
        lv_obj_set_width(s_detailCode[i], LV_SIZE_CONTENT);

        s_detailMsg[i] = makeLabel(s_detailRows[i], COL_MSG);
        lv_label_set_text(s_detailMsg[i], "");
        lv_label_set_long_mode(s_detailMsg[i], LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(s_detailMsg[i], 1);

        // Per-row dismiss button — captures row index via static array offset
        s_detailDism[i] = makeDismissBtn(s_detailRows[i]);
    }

    // Wire dismiss for rows 0..MAX_ROWS-1 using individual lambdas (no closure capture in C++11)
    // We use a generic callback and retrieve the row index from user_data.
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        // Pass the row label pointer as user data to identify which error to dismiss.
        // On click, we dismiss the oldest of the currently-shown errors for that slot.
        lv_obj_add_event_cb(
            s_detailDism[i],
            [](lv_event_t *e) {
                lv_event_stop_bubbling(e);
                // Identify which row was clicked via user_data (row index stored as pointer)
                uintptr_t row = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
                // Dismiss the error at position 'row' (newest=0) by rebuilding:
                // We can only dismiss latest via ErrorStore. For now dismiss latest.
                // TODO: add dismiss-by-index to ErrorStore for per-row dismiss.
                (void)row;
                ErrorStore::dismissLatest();
                if (ErrorStore::getCount() == 0)
                    setExpanded(false);
            },
            LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
}

void ErrorBar::update() {
    if (!s_container)
        return;

    const uint32_t version = ErrorStore::getVersion();
    if (version == s_lastVersion)
        return; // Nothing changed
    s_lastVersion = version;

    const uint8_t count = ErrorStore::getCount();

    if (count == 0) {
        lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
        s_expanded = false;
        return;
    }

    // Show bar
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);

    // Fetch all errors (newest first)
    FwError errors[MAX_ROWS];
    uint8_t fetched = 0;
    ErrorStore::getAll(errors, &fetched, MAX_ROWS);

    // ---------------------------------------------------------------------------
    // Update header (latest = errors[0])
    // ---------------------------------------------------------------------------
    {
        char codeBuf[20];
        snprintf(codeBuf, sizeof(codeBuf), "\xE2\x9A\xA0 %s:%s", srcLabel(errors[0].source),
                 errors[0].code);
        lv_label_set_text(s_codeLabel, codeBuf);
        lv_label_set_text(s_msgLabel, errors[0].message);

        if (count > 1) {
            char countBuf[8];
            snprintf(countBuf, sizeof(countBuf), "+%u", count - 1);
            lv_label_set_text(s_countLabel, countBuf);
            lv_obj_clear_flag(s_countLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_countLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ---------------------------------------------------------------------------
    // Update detail rows
    // ---------------------------------------------------------------------------
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        if (!s_detailRows[i])
            continue;
        if (i < fetched) {
            char codeBuf[20];
            snprintf(codeBuf, sizeof(codeBuf), "%s:%s", srcLabel(errors[i].source), errors[i].code);
            lv_label_set_text(s_detailCode[i], codeBuf);
            lv_label_set_text(s_detailMsg[i], errors[i].message);
            lv_obj_clear_flag(s_detailRows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_detailRows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Resize container to fit header + (expanded ? detail rows : 0)
    const int16_t detailH = s_expanded ? static_cast<int16_t>(fetched) * ROW_H : 0;
    lv_obj_set_height(s_container, BAR_H + detailH);
    // Re-align to bottom after height change
    lv_obj_align(s_container, LV_ALIGN_BOTTOM_MID, 0, 0);
}
