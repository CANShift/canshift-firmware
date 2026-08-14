#include "alert_banner.h"
#include "layout_scale.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "top_bar.h"
#include "ui/alert_sources.h"
#include "ui/severity.h"
#include "ui/widgets/widget_helpers.h"
#include "util/format_float.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kUpdatePeriodMs = 250;
constexpr size_t kTextBufLen = 96;
constexpr int16_t kBannerTopGapPx = 2;
constexpr int16_t kRowGapPx = 4;
constexpr const char *kSeparator = " · ";

bool appendPart(char *buf, size_t len, size_t &used, const char *part) {
    const int n = snprintf(buf + used, len - used, "%s%s", (used > 0) ? kSeparator : "", part);
    if (n < 0 || static_cast<size_t>(n) >= len - used)
        return false;
    used += static_cast<size_t>(n);
    return true;
}

void composeCriticalText(const AlertEngine::AlertState &state, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (const AlertSources::CriticalSource &src : AlertSources::kSources) {
        if (state.*(src.level) != AlertEngine::AlertLevel::CRITICAL)
            continue;
        if (state.*(src.sensorLost) && !SignalStore::isValid(src.id))
            continue;
        char valBuf[16];
        FloatFormat::formatFixed(valBuf, sizeof(valBuf), SignalStore::read(src.id), src.decimals);
        char part[32];
        snprintf(part, sizeof(part), "%s %s%s", src.chipLabel, valBuf, src.unit);
        if (!appendPart(buf, len, used, part))
            return;
    }
}

void composeSensorLostText(const AlertEngine::AlertState &state, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (const AlertSources::CriticalSource &src : AlertSources::kSources) {
        if (!(state.*(src.sensorLost)))
            continue;
        if (!appendPart(buf, len, used, src.chipLabel))
            return;
    }
}

void composeMilText(const AlertEngine::AlertState &state, char *buf, size_t len) {
    strlcpy(buf, state.milActive ? "ECU FAULT STORED" : "", len);
}

struct BannerRow {
    const char *kicker;
    Severity::Level level;
    void (*compose)(const AlertEngine::AlertState &, char *, size_t);
};

constexpr BannerRow kRows[] = {
    {"CRITICAL", Severity::Level::FAILURE, composeCriticalText},
    {"SENSOR LOST", Severity::Level::WARNING, composeSensorLostText},
    {"CHECK ENGINE", Severity::Level::WARNING, composeMilText},
};

constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

lv_obj_t *s_container = nullptr;
Severity::Surface s_surfaces[kRowCount];
lv_obj_t *s_reasons[kRowCount] = {nullptr};
uint32_t s_lastUpdateMs = 0;

lv_obj_t *createContainer() {
    lv_obj_t *c = lv_obj_create(lv_layer_top());
    WidgetHelpers::resetContainerStyle(c);
    lv_obj_set_size(c, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 0, TopBar::getHeight() + LayoutScale::y(kBannerTopGapPx));
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c, kRowGapPx, LV_PART_MAIN);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

void buildRow(size_t i) {
    const Severity::Spec spec = {kRows[i].level, kRows[i].kicker, Severity::kRulePrimaryPx};
    s_surfaces[i] = Severity::build(s_container, spec);
    s_reasons[i] = Severity::addReason(s_surfaces[i], "");
    WidgetHelpers::setVisible(s_surfaces[i].root, false);
}

bool renderRow(size_t i, const AlertEngine::AlertState &state) {
    char text[kTextBufLen];
    kRows[i].compose(state, text, sizeof(text));
    const bool visible = text[0] != '\0';
    if (visible)
        WidgetHelpers::setLabelTextIfChanged(s_reasons[i], text);
    WidgetHelpers::setVisibleIfChanged(s_surfaces[i].root, visible);
    return visible;
}

} // namespace

void AlertBanner::init() {
    s_container = createContainer();
    for (size_t i = 0; i < kRowCount; ++i) {
        buildRow(i);
    }
    s_lastUpdateMs = 0;
}

void AlertBanner::update() {
    if (!s_container)
        return;

    const uint32_t now = millis();
    if (now - s_lastUpdateMs < kUpdatePeriodMs)
        return;
    s_lastUpdateMs = now;

    const AlertEngine::AlertState state = AlertEngine::getState();
    bool anyVisible = false;
    for (size_t i = 0; i < kRowCount; ++i) {
        anyVisible = renderRow(i, state) || anyVisible;
    }
    WidgetHelpers::setVisibleIfChanged(s_container, anyVisible);
}
