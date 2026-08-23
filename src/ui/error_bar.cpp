#include "error_bar.h"
#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "diag/lvgl_pool.h"
#include "layout_scale.h"
#include "ui/font_manager.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kDismissMinHeapBytes = 1024;
constexpr uint8_t kMaxRows = ERROR_STORE_RING_SIZE;
constexpr int16_t kDetailMaxH = 160;
constexpr int16_t kRightInsetPx = 8;
constexpr int16_t kRowGapPx = 4;
constexpr int16_t kBottomPadPx = 4;
constexpr size_t kKickerLen = 28;
constexpr const char *kWayOut[] = {"TAP TO DISMISS", "TAP A LINE TO DISMISS"};

struct SourceLevel {
    ErrorSource source;
    Severity::Level level;
};

constexpr SourceLevel kSourceLevels[] = {{ERROR_SRC_CONFIG, Severity::Level::WARNING}};

Severity::Surface s_header;
lv_obj_t *s_container = nullptr;
lv_obj_t *s_msgLabel = nullptr;
lv_obj_t *s_wayOutLabel = nullptr;
lv_obj_t *s_detailPanel = nullptr;
Severity::Surface s_rows[kMaxRows];
lv_obj_t *s_rowMsg[kMaxRows] = {nullptr};

bool s_expanded = false;
uint32_t s_lastVersion = UINT32_MAX;

bool heapHealthyForLvglUpdate() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kDismissMinHeapBytes;
}

Severity::Level levelFor(ErrorSource source) {
    for (const SourceLevel &entry : kSourceLevels) {
        if (entry.source == source)
            return entry.level;
    }
    return Severity::Level::FAILURE;
}

void composeKicker(const FwError &err, uint8_t extra, char *out, size_t len) {
    if (extra == 0) {
        snprintf(out, len, "%s:%s", ErrorStore::sourceLabel(err.source), err.code);
        return;
    }
    snprintf(out, len, "%s:%s +%u", ErrorStore::sourceLabel(err.source), err.code, extra);
}

lv_obj_t *makeDimLine(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FontManager::value(Severity::kReasonPx), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::dimColor()), 0);
    return label;
}

void setExpanded(bool expand) {
    s_expanded = expand;
    WidgetHelpers::setVisible(s_detailPanel, expand);
}

void onContainerGesture(lv_event_t *) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM)
        return;
    lv_indev_wait_release(lv_indev_get_act());
    setExpanded(dir == LV_DIR_TOP);
}

void onHeaderClicked(lv_event_t *) {
    if (ErrorStore::getCount() > 1) {
        setExpanded(!s_expanded);
        return;
    }
    ErrorStore::clear();
    setExpanded(false);
}

void onRowClicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    const uintptr_t row = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (!heapHealthyForLvglUpdate()) {
        LOG_WARN("ERRBAR", "row dismiss deferred — heap.largest=%u below floor",
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    }
    ErrorStore::dismissAt(static_cast<uint8_t>(row));
    if (ErrorStore::getCount() == 0)
        setExpanded(false);
}

lv_obj_t *createContainer() {
    lv_obj_t *c = lv_obj_create(lv_layer_top());
    WidgetHelpers::resetContainerStyle(c);
    lv_obj_set_width(c, LV_HOR_RES);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    const CfgColor ground = {
        ThemeManager::pickColor(ThemeTokens::kGroundNight, ThemeTokens::kGroundDay)};
    lv_obj_set_style_bg_color(c, lv_color_hex(ThemeManager::getEffectiveBgColor(ground).rgb),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_right(c, kRightInsetPx, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(c, kBottomPadPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c, kRowGapPx, LV_PART_MAIN);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(c, onContainerGesture, LV_EVENT_GESTURE, nullptr);
    return c;
}

void buildHeader(lv_obj_t *parent) {
    const Severity::Spec spec = {Severity::Level::FAILURE, "", Severity::kRulePrimaryPx};
    s_header = Severity::build(parent, spec);
    if (!s_header.root)
        return;
    lv_obj_add_flag(s_header.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_header.root, onHeaderClicked, LV_EVENT_CLICKED, nullptr);
    s_msgLabel = Severity::addReason(s_header, "");
    s_wayOutLabel = makeDimLine(s_header.value, kWayOut[0]);
    if (!s_msgLabel || !s_wayOutLabel)
        LOG_ERROR("ERRBAR", "header labels not created — LVGL pool OOM");
}

lv_obj_t *createDetailPanel(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(p);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(p, LayoutScale::y(kDetailMaxH), LV_PART_MAIN);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, kRowGapPx, LV_PART_MAIN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void buildDetailRow(lv_obj_t *panel, uint8_t i) {
    s_rowMsg[i] = nullptr;
    const Severity::Spec spec = {Severity::Level::FAILURE, "", Severity::kRuleSecondaryPx};
    s_rows[i] = Severity::build(panel, spec);
    if (!s_rows[i].root)
        return;
    s_rowMsg[i] = Severity::addReason(s_rows[i], "");
    lv_obj_add_flag(s_rows[i].root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rows[i].root, onRowClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
}

void renderHeader(const FwError &err, uint8_t count) {
    char kicker[kKickerLen];
    composeKicker(err, static_cast<uint8_t>(count - 1), kicker, sizeof(kicker));
    Severity::repaint(s_header, levelFor(err.source));
    Severity::setKicker(s_header, kicker);
    WidgetHelpers::setLabelTextIfChanged(s_msgLabel, err.message);
    WidgetHelpers::setLabelTextIfChanged(s_wayOutLabel, kWayOut[count > 1 ? 1 : 0]);
}

void renderRow(uint8_t i, const FwError &err) {
    char kicker[kKickerLen];
    composeKicker(err, 0, kicker, sizeof(kicker));
    Severity::repaint(s_rows[i], levelFor(err.source));
    Severity::setKicker(s_rows[i], kicker);
    WidgetHelpers::setLabelTextIfChanged(s_rowMsg[i], err.message);
}

void renderRows(const FwError *errors, uint8_t fetched) {
    for (uint8_t i = 0; i < kMaxRows; ++i) {
        if (!s_rows[i].root)
            continue;
        WidgetHelpers::setVisible(s_rows[i].root, i < fetched);
        if (i < fetched)
            renderRow(i, errors[i]);
    }
}

void buildBar() {
    if (s_container)
        return;
    if (!LvglPool::hasHeadroomFor(LvglPool::kDeferredSurfaceBytes, "ErrorBar"))
        return;
    s_container = createContainer();
    if (!s_container)
        return;
    buildHeader(s_container);
    s_detailPanel = createDetailPanel(s_container);
    for (uint8_t i = 0; i < kMaxRows; ++i) {
        buildDetailRow(s_detailPanel, i);
    }
}

} // namespace

void ErrorBar::reapplyTheme() {
    if (!s_container)
        return;
    lv_obj_del(s_container);
    s_container = nullptr;
    s_expanded = false;
    s_lastVersion = UINT32_MAX;
    buildBar();
}

void ErrorBar::update() {
    const uint32_t version = ErrorStore::getVersion();
    if (version == s_lastVersion)
        return;
    if (!heapHealthyForLvglUpdate())
        return;

    const uint8_t count = ErrorStore::getCount();
    if (count == 0) {
        s_lastVersion = version;
        if (!s_container)
            return;
        WidgetHelpers::setVisible(s_container, false);
        s_expanded = false;
        return;
    }

    buildBar();
    if (!s_container)
        return;
    s_lastVersion = version;

    FwError errors[kMaxRows];
    uint8_t fetched = 0;
    ErrorStore::getAll(errors, &fetched, kMaxRows);
    if (fetched == 0)
        return;

    renderHeader(errors[0], count);
    renderRows(errors, fetched);
    WidgetHelpers::setVisible(s_container, true);
}
