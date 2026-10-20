#include "g_meter_widget.h"

#include "diag/logger.h"
#include "runtime/imu_feed.h"
#include "ui/control_vocabulary.h"
#include "ui/font_manager.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/g_meter_plot.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "util/format_float.h"

#include <lvgl.h>
#include <stdio.h>

namespace {

constexpr const char *kDefaultKicker = "G";
constexpr const char *kNoImuReason = "NO IMU";
constexpr const char *kUnit = "g";
constexpr int16_t kUnitGapPx = 2;
constexpr size_t kValueTextCap = 8;

struct GMeterTag {
    GMeterPlot::Plot plot;
    lv_obj_t *value;
    float rangeG;
    bool peakHold;
    bool locked;
    uint32_t lastValueRgb;
};

const char *kickerOf(const CfgGMeterParams &params) {
    return params.kicker[0] != '\0' ? params.kicker : kDefaultKicker;
}

void buildLockedHeader(lv_obj_t *cont, const CfgGMeterParams &params) {
    char text[WidgetLabelOverlay::kKickerBufLen];
    snprintf(text, sizeof(text), "%s%s%s", kickerOf(params), ControlVocabulary::kSeparator,
             kNoImuReason);
    lv_obj_t *kicker = WidgetLabelOverlay::applyWidgetHeader(cont, text, "");
    if (kicker)
        lv_obj_set_style_text_color(kicker, lv_color_hex(ThemeManager::lockInkColor()), 0);
}

lv_obj_t *buildReadout(lv_obj_t *cont) {
    lv_obj_t *row = lv_obj_create(cont);
    WidgetHelpers::resetContainerStyle(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, GMeterMetrics::kReadoutTopPx);
    lv_obj_set_style_pad_column(row, kUnitGapPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_style_text_font(value, FontManager::value(GMeterMetrics::kValueFontPx), 0);
    lv_obj_set_style_text_color(value, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_t *unit = lv_label_create(row);
    lv_obj_set_style_text_font(unit, FontManager::units(), 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(ThemeManager::dimColor()), 0);
    lv_label_set_text(unit, kUnit);
    return value;
}

void showValue(GMeterTag *tag, const char *text, uint32_t rgb) {
    WidgetHelpers::setLabelTextIfChanged(tag->value, text);
    WidgetStyles::setTextColorIfChanged(tag->value, tag->lastValueRgb, rgb);
}

void showStale(GMeterTag *tag) {
    GMeterPlot::hideLive(tag->plot);
    char text[kValueTextCap];
    WidgetHelpers::formatStalePlaceholder(text, sizeof(text), tag->rangeG);
    showValue(tag, text, ThemeManager::getStaleTextColor());
}

void showLive(GMeterTag *tag, const ImuCoreState &core) {
    GMeterPlot::showDot(tag->plot, core, tag->rangeG);
    if (tag->peakHold)
        GMeterPlot::showEnvelope(tag->plot, core, tag->rangeG);
    char text[kValueTextCap];
    FloatFormat::formatFixed(text, sizeof(text), imu_core_combined_g_rs(&core),
                             GMeterMetrics::kValueDecimals);
    showValue(tag, text, ThemeManager::getEffectiveTextColor());
}

void onTap(lv_event_t *) {
    ImuFeed::requestPeakReset();
}

void initTag(GMeterTag *tag, const CfgWidget &cfg, bool present) {
    tag->rangeG = cfg.gMeter.rangeG;
    tag->peakHold = cfg.gMeter.peakHold;
    tag->locked = !present;
    tag->lastValueRgb = ThemeManager::getStaleTextColor();
}

void decorate(lv_obj_t *cont, GMeterTag *tag, const CfgWidget &cfg) {
    const uint32_t ruleRgb = tag->locked ? ThemeManager::lockLineColor()
                                         : ThemeManager::getEffectiveTextColor(
                                               cfg.style.textColor.rgb, cfg.style.respectDayMode);
    WidgetHelpers::makeTopRule(cont, Severity::kRulePrimaryPx, ruleRgb);
    GMeterPlot::paintGuides(tag->plot, tag->locked ? ThemeManager::lockLineColor()
                                                   : ThemeManager::trackColor());
    if (tag->locked) {
        buildLockedHeader(cont, cfg.gMeter);
        return;
    }
    WidgetLabelOverlay::applyWidgetHeader(cont, kickerOf(cfg.gMeter), "");
    tag->value = buildReadout(cont);
    showStale(tag);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont, onTap, LV_EVENT_CLICKED, nullptr);
}

}

lv_obj_t *GMeterWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);
    WidgetTagPool::Slot<GMeterTag> tagSlot;
    GMeterTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "GMETER", cont);
    if (!tag)
        return nullptr;
    const WidgetHelpers::ScaledBox box = WidgetHelpers::scaledBox(cfg);
    if (!GMeterPlot::build(cont, GMeterMetrics::plotBox(box.w, box.h), tag->plot)) {
        LOG_ERROR("WF", "g-meter '%s' plot could not be built", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    initTag(tag, cfg, ImuFeed::present());
    decorate(cont, tag, cfg);
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GMeterTag>, LV_EVENT_DELETE,
                        tagSlot.commit());
    return cont;
}

void GMeterWidget::update(lv_obj_t *obj) {
    if (!obj)
        return;
    auto *tag = static_cast<GMeterTag *>(lv_obj_get_user_data(obj));
    if (!tag || tag->locked)
        return;
    ImuCoreState core = {};
    if (!ImuFeed::latest(core)) {
        showStale(tag);
        return;
    }
    showLive(tag, core);
}
