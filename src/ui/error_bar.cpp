// error_bar.cpp — Persistent bottom error overlay (lv_layer_top).

#include "error_bar.h"
#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "ui/font_manager.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// Minimum largest-free-block needed before we let an LVGL-touching dismiss
// handler run. The dismiss path eventually rehides `s_container`, which
// triggers LVGL invalidate + layout reflow; both allocate from the LVGL
// pool. Under the heap fragmentation that follows a theme toggle / page
// rebuild (issue #973), those allocations fail and the assert handler
// resets the ESP. Below this floor we bail the visual update and only
// clear the store — `ErrorBar::update()` will retry the visual on the
// next tick once the heap has recovered.
static constexpr size_t DISMISS_MIN_HEAP_BYTES = 1024;

static inline bool heapHealthyForLvglUpdate() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= DISMISS_MIN_HEAP_BYTES;
}

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static constexpr int16_t BAR_H = 20; // Collapsed height (px)
static constexpr int16_t ROW_H = 20; // Height of each expanded row
// Mirrors the ErrorStore ring capacity (`RING_SIZE` in `error_store.cpp`).
// Pre-allocating exactly that many detail rows means we never touch the heap
// on the UI hot path while still showing every error the store can hold.
// Issue #642.
static constexpr uint8_t MAX_ROWS = 6;
// Detail panel cap — beyond this LVGL scrolls the rows vertically.
// 120 = 6 rows × 20 px; the cap exists so future ring-size bumps can't push
// the drawer past the dashboard's visible area.
static constexpr int16_t DETAIL_MAX_H = 120;

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
    // Wider than tall so the right-edge tap target is comfortable on the
    // resistive touch panel. The previous 20×20 hit area was both small and
    // sat flush against the screen edge where calibration is least accurate
    // — users frequently reported "the X doesn't close the error" because
    // the tap landed outside the button bounds.
    lv_obj_set_size(btn, 32, BAR_H);
    // Subtle filled background so the target is visible, not just the glyph.
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A1010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    // Uppercase X is wider and renders clearly in the bundled Orbitron font
    // (lowercase "x" was too thin to read against a dark error background).
    lv_label_set_text(lbl, "X");
    lv_obj_set_style_text_font(lbl, FONT(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MSG), 0);
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
// init() phase helpers
// ---------------------------------------------------------------------------

// Swipe up = expand, swipe down = collapse (issue #642). The container sits
// on `lv_layer_top()` above the pages, so its gesture handler runs before
// PageManager's horizontal swipe — UP/DOWN never conflict with page LEFT/RIGHT.
static void onContainerGesture(lv_event_t *e) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        setExpanded(true);
    } else if (dir == LV_DIR_BOTTOM) {
        setExpanded(false);
    }
    (void)e;
}

// Dismiss button: clear all errors and stop propagation (don't toggle expand).
// Heap-guarded so a tap doesn't reboot the ESP when the post-rebuild pool
// is too fragmented for LVGL's cascaded invalidate to allocate (#973).
static void onHeaderDismissClicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    if (!heapHealthyForLvglUpdate()) {
        LOG_WARN("ERRBAR", "dismiss deferred — heap.largest=%u below floor",
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        ErrorStore::clear(); // State-only; ErrorBar::update retries on next tick.
        return;
    }
    ErrorStore::clear();
    setExpanded(false);
}

// Per-row dismiss buttons. `getAll` returns newest-first into the same row
// slots, so the `i` index baked into the lambda's user_data maps directly to
// ErrorStore's newest-first row index (issue #898). Same heap guard as the
// header dismiss (#973).
static void onRowDismissClicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    const uintptr_t row = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (!heapHealthyForLvglUpdate()) {
        LOG_WARN("ERRBAR", "row dismiss deferred — heap.largest=%u below floor",
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        ErrorStore::dismissAt(static_cast<uint8_t>(row));
        return;
    }
    ErrorStore::dismissAt(static_cast<uint8_t>(row));
    if (ErrorStore::getCount() == 0)
        setExpanded(false);
}

// Outer container — flush to the bottom of lv_layer_top(). Height tracks the
// detail panel's visibility (LV_SIZE_CONTENT) so the collapsed bar is always
// exactly BAR_H and the expanded variant grows just enough to show its
// scrollable rows (#642).
static lv_obj_t *createContainer() {
    lv_obj_t *c = lv_obj_create(lv_layer_top());
    lv_obj_set_width(c, LV_HOR_RES);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, lv_color_hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN); // Hidden until first error
    lv_obj_add_event_cb(c, onContainerGesture, LV_EVENT_GESTURE, nullptr);
    return c;
}

// Header row — always visible when bar is shown. Populates the file-scope
// label/button slots so `update()` and the dismiss callback can reach them.
static void buildHeaderRow(lv_obj_t *parent) {
    s_headerRow = lv_obj_create(parent);
    lv_obj_set_size(s_headerRow, LV_PCT(100), BAR_H);
    applyRowStyle(s_headerRow);
    lv_obj_set_style_pad_left(s_headerRow, 4, LV_PART_MAIN);
    // Stop header row clicks from reaching the container's expand toggle
    lv_obj_clear_flag(s_headerRow, LV_OBJ_FLAG_CLICKABLE);

    s_codeLabel = makeLabel(s_headerRow, COL_CODE);
    // LV_SYMBOL_WARNING is in the default ROM font but not in the SPIFFS Orbitron
    // binaries — use LV_FONT_DEFAULT so the symbol renders correctly.
    lv_obj_set_style_text_font(s_codeLabel, LV_FONT_DEFAULT, 0);
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
    lv_obj_add_event_cb(s_dismissBtn, onHeaderDismissClicked, LV_EVENT_CLICKED, nullptr);
}

// Detail panel — shown when expanded. Caps rendered height so the drawer never
// pushes past the dashboard; beyond DETAIL_MAX_H the rows scroll inside the
// panel (#642).
static lv_obj_t *createDetailPanel(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(p, DETAIL_MAX_H, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x100505), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_hex(0x2A1010), LV_PART_MAIN);
    lv_obj_set_style_border_side(p, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(p, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// One detail row + its labels + per-row dismiss button. Index `i` is wired to
// the dismiss callback's user_data in `ErrorBar::init` so it maps to the same
// newest-first slot used by `update()` when calling `ErrorStore::dismissAt(i)`.
static void buildDetailRow(lv_obj_t *panel, uint8_t i) {
    s_detailRows[i] = lv_obj_create(panel);
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ErrorBar::init() {
    s_container = createContainer();
    buildHeaderRow(s_container);

    s_detailPanel = createDetailPanel(s_container);
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        buildDetailRow(s_detailPanel, i);
    }
    // Wire per-row dismiss callbacks in a second pass — keeps row construction
    // and event wiring as separable phases.
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        lv_obj_add_event_cb(s_detailDism[i], onRowDismissClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
}

void ErrorBar::update() {
    if (!s_container)
        return;

    const uint32_t version = ErrorStore::getVersion();
    if (version == s_lastVersion)
        return; // Nothing changed

    // Defer the visual sync when the LVGL pool is too fragmented to safely
    // reflow (#973). Leave `s_lastVersion` untouched so the next tick (or
    // the one after, once the heap has coalesced enough) picks the new
    // version up and finishes the visual update.
    if (!heapHealthyForLvglUpdate())
        return;

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
        snprintf(codeBuf, sizeof(codeBuf), "%s:%s", srcLabel(errors[0].source), errors[0].code);
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

    // Re-align to bottom after the LV_SIZE_CONTENT-driven height change so
    // the bar stays flush with the bottom edge regardless of row count.
    lv_obj_align(s_container, LV_ALIGN_BOTTOM_MID, 0, 0);
}
