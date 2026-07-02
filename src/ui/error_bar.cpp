#include "error_bar.h"
#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "ui/font_manager.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t DISMISS_MIN_HEAP_BYTES = 1024;

static inline bool heapHealthyForLvglUpdate() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= DISMISS_MIN_HEAP_BYTES;
}

static constexpr int16_t BAR_H = 32;
static constexpr int16_t ROW_H = 32;

static constexpr int16_t DISMISS_W = 44;
static constexpr int16_t DISMISS_H = 32;

static constexpr int16_t SOURCE_STRIP_W = 4;

static constexpr uint8_t MAX_ROWS = 6;
static constexpr int16_t DETAIL_MAX_H = 160;

static constexpr uint32_t COL_BG = 0x160808;
static constexpr uint32_t COL_BORDER_CRITICAL = 0xCC3333;
static constexpr uint32_t COL_BORDER_CONFIG = 0xCC8800;
static constexpr uint32_t COL_CODE = 0xCC4444;
static constexpr uint32_t COL_MSG = 0xDDAAAA;
static constexpr uint32_t COL_DIM = 0x664444;

static inline uint32_t sourceStripColor(ErrorSource src) {
    return src == ERROR_SRC_CONFIG ? COL_BORDER_CONFIG : COL_BORDER_CRITICAL;
}

static inline const lv_font_t *FONT() {
    return FontManager::label(12);
}

static lv_obj_t *s_container = nullptr;
static lv_obj_t *s_headerRow = nullptr;
static lv_obj_t *s_codeLabel = nullptr;
static lv_obj_t *s_msgLabel = nullptr;
static lv_obj_t *s_countLabel = nullptr;
static lv_obj_t *s_chevronLabel = nullptr;
static lv_obj_t *s_dismissBtn = nullptr;

static lv_obj_t *s_detailPanel = nullptr;
static lv_obj_t *s_detailRows[MAX_ROWS] = {nullptr};
static lv_obj_t *s_detailCode[MAX_ROWS] = {nullptr};
static lv_obj_t *s_detailMsg[MAX_ROWS] = {nullptr};
static lv_obj_t *s_detailDism[MAX_ROWS] = {nullptr};

static bool s_expanded = false;
static uint32_t s_lastVersion = UINT32_MAX;

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

    lv_obj_set_size(btn, DISMISS_W, DISMISS_H);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A1010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
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

static void onContainerGesture(lv_event_t *e) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        /* swallow the release so the swipe does not also toggle via the header click */
        lv_indev_wait_release(lv_indev_get_act());
        setExpanded(true);
    } else if (dir == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        setExpanded(false);
    }
    (void)e;
}

static void onHeaderClicked(lv_event_t *) {
    setExpanded(!s_expanded);
}

static void onHeaderDismissClicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    if (!heapHealthyForLvglUpdate()) {
        LOG_WARN("ERRBAR", "dismiss deferred — heap.largest=%u below floor",
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

        ErrorStore::clear();
        return;
    }
    ErrorStore::clear();
    setExpanded(false);
}

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

static lv_obj_t *createContainer() {
    lv_obj_t *c = lv_obj_create(lv_layer_top());
    lv_obj_set_width(c, LV_HOR_RES);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, lv_color_hex(COL_BORDER_CRITICAL), LV_PART_MAIN);
    lv_obj_set_style_border_width(c, SOURCE_STRIP_W, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(c, onContainerGesture, LV_EVENT_GESTURE, nullptr);
    return c;
}

static void buildHeaderRow(lv_obj_t *parent) {
    s_headerRow = lv_obj_create(parent);
    lv_obj_set_size(s_headerRow, LV_PCT(100), BAR_H);
    applyRowStyle(s_headerRow);
    lv_obj_set_style_pad_left(s_headerRow, 4, LV_PART_MAIN);

    lv_obj_add_flag(s_headerRow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_headerRow, onHeaderClicked, LV_EVENT_CLICKED, nullptr);

    s_codeLabel = makeLabel(s_headerRow, COL_CODE);

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

    s_chevronLabel = makeLabel(s_headerRow, COL_CODE);
    lv_label_set_text(s_chevronLabel, "^");
    lv_obj_set_width(s_chevronLabel, LV_SIZE_CONTENT);
    lv_obj_add_flag(s_chevronLabel, LV_OBJ_FLAG_HIDDEN);

    s_dismissBtn = makeDismissBtn(s_headerRow);
    lv_obj_add_event_cb(s_dismissBtn, onHeaderDismissClicked, LV_EVENT_CLICKED, nullptr);
}

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

    s_detailDism[i] = makeDismissBtn(s_detailRows[i]);
}

void ErrorBar::init() {
    s_container = createContainer();
    buildHeaderRow(s_container);

    s_detailPanel = createDetailPanel(s_container);
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        buildDetailRow(s_detailPanel, i);
    }
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
        return;

    if (!heapHealthyForLvglUpdate())
        return;

    s_lastVersion = version;

    const uint8_t count = ErrorStore::getCount();

    if (count == 0) {
        lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
        s_expanded = false;
        return;
    }

    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);

    FwError errors[MAX_ROWS];
    uint8_t fetched = 0;
    ErrorStore::getAll(errors, &fetched, MAX_ROWS);

    lv_obj_set_style_border_color(s_container, lv_color_hex(sourceStripColor(errors[0].source)),
                                  LV_PART_MAIN);

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
            lv_obj_clear_flag(s_chevronLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_countLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chevronLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

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

    lv_obj_align(s_container, LV_ALIGN_BOTTOM_MID, 0, 0);
}
