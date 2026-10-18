#include "gauge_widget.h"
#include "gauge_arcs.h"
#include "ui/cut_band.h"
#include "ui/font_manager.h"
#include "ui/rev_limit_flash.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/unit_display.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "config/config_loader.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <algorithm>
#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

static constexpr int16_t kValueFontHeightPrimary = 90;
static constexpr int16_t kValueFontHeightSecondary = 40;
static constexpr uint8_t kValueFontSizePrimary = 48;
static constexpr uint8_t kValueFontSizeSecondary = 17;
static constexpr uint8_t kValueFontSizeUnits = 10;

static int32_t scaleForDisplay(float value, uint8_t decimals) {
    static constexpr float kPow10[] = {1.0f, 10.0f, 100.0f, 1000.0f};
    const uint8_t cappedDecimals = decimals > 3 ? 3 : decimals;
    const float scaled = value * kPow10[cappedDecimals];
    if (scaled >= 2147483640.0f)
        return 2147483640;
    if (scaled <= -2147483640.0f)
        return -2147483640;
    return static_cast<int32_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

struct GaugeTag {
    lv_obj_t *valueLabel;
    lv_obj_t *fillArc;
    Severity::Surface severity;
    float minValue;
    float maxValue;
    float lastValue;
    uint16_t dangerAngle;
    bool hasDanger;
    bool lastValid;
    bool revFlash;
    bool lastLimiting;
    uint32_t inkRgb;
    float warnLevel;
    float dangerLevel;
    Severity::SignalLimits catalogue;
    uint32_t lastLabelRgb;
    uint32_t lastFillRgb;
    uint16_t lastAngle;
    int32_t lastDisplayScaled;
    char stalePlaceholder[WidgetHelpers::kStalePlaceholderCap];
    SignalId signal;
    Severity::Level cutLevel;
    const char *convertibleUnit;
};

static uint32_t gaugeInkFor(const GaugeTag *tag, Severity::Level level) {
    if (level == Severity::Level::INFORMATION)
        return tag->inkRgb;
    return Severity::inkFor(level);
}

static Severity::Level gaugeLevelFor(const GaugeTag *tag, float value, bool dangerBelow,
                                     bool limiting) {
    if (limiting)
        return Severity::Level::FAILURE;
    const Severity::Level catalogue = Severity::forSignal(value, tag->catalogue);
    if (!tag->hasDanger)
        return catalogue;
    return Severity::strongerOf(
        Severity::forReading(value, tag->warnLevel, tag->dangerLevel, dangerBelow), catalogue);
}

static const lv_font_t *resolveValueFont(const CfgWidget &cfg, uint8_t &intFontSizeOut) {
    if (cfg.gauge.big > 0) {
        intFontSizeOut = WidgetHelpers::deviceFontPxForBig(cfg.gauge.big);
        return FontManager::value(intFontSizeOut);
    }
    const int16_t h = WidgetHelpers::scaledBox(cfg).h;
    if (h >= kValueFontHeightPrimary) {
        intFontSizeOut = kValueFontSizePrimary;
        return FontManager::value(kValueFontSizePrimary);
    }
    if (h >= kValueFontHeightSecondary) {
        intFontSizeOut = kValueFontSizeSecondary;
        return FontManager::value(kValueFontSizeSecondary);
    }
    intFontSizeOut = kValueFontSizeUnits;
    return FontManager::units();
}

static constexpr int16_t kValueRowYOffset = GaugeArcs::kArcYShift;
static constexpr int16_t kValueClusterInsetPx = 0;

static lv_obj_t *buildValueRow(lv_obj_t *cont) {
    lv_obj_t *valueRow = lv_obj_create(cont);
    lv_obj_set_size(valueRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(valueRow, LV_ALIGN_LEFT_MID, kValueClusterInsetPx, kValueRowYOffset);
    WidgetHelpers::resetContainerStyle(valueRow);
    lv_obj_set_style_pad_column(valueRow, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    return valueRow;
}

static lv_obj_t *buildValueLabel(lv_obj_t *valueRow, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(valueRow);
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return label;
}

struct GaugeArcModes {
    uint16_t dangerAngle;
    bool hasDanger;
};

static GaugeArcModes resolveArcModes(const CfgWidget &cfg) {
    GaugeArcModes m{};
    const float minV = cfg.gauge.minValue;
    const float maxV = cfg.gauge.maxValue;
    m.hasDanger = !std::isnan(cfg.gauge.dangerLevel) && cfg.gauge.dangerLevel > minV &&
                  cfg.gauge.dangerLevel < maxV;
    m.dangerAngle = m.hasDanger ? GaugeArcs::valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;
    return m;
}

struct GaugeBuildState {
    lv_obj_t *label;
    lv_obj_t *fillArc;
    lv_obj_t *topRule;
    lv_obj_t *kicker;
    uint8_t rulePx;
    uint16_t dangerAngle;
    bool hasDanger;
};

static void buildValueCluster(lv_obj_t *cont, const CfgWidget &cfg, lv_obj_t *&outLabel,
                              lv_obj_t *&outKicker) {
    uint8_t intFontSize = kValueFontSizeUnits;
    const lv_font_t *font = resolveValueFont(cfg, intFontSize);
    lv_obj_t *valueRow = buildValueRow(cont);
    outLabel = buildValueLabel(valueRow, font);
    if (outLabel)
        lv_obj_set_style_text_letter_space(outLabel, WidgetHelpers::valueTrackingPx(intFontSize),
                                           0);
    outKicker = WidgetLabelOverlay::applyWidgetHeader(cont, cfg.gauge.kicker, cfg.signalId);
}

static void initGaugeTag(GaugeTag *tag, const CfgWidget &cfg, const GaugeBuildState &built,
                         uint32_t inkRgb) {
    tag->valueLabel = built.label;
    tag->fillArc = built.fillArc;
    tag->severity = Severity::adopt(built.topRule, built.kicker, nullptr, built.rulePx, inkRgb);
    tag->minValue = cfg.gauge.minValue;
    tag->maxValue = cfg.gauge.maxValue;
    tag->lastValue = NAN;
    tag->dangerAngle = built.dangerAngle;
    tag->hasDanger = built.hasDanger;
    tag->inkRgb = inkRgb;
    tag->lastValid = false;
    tag->lastLabelRgb = ThemeManager::getStaleTextColor();
    tag->lastFillRgb = 0xFFFFFFFFu;
    tag->lastAngle = 0xFFFFu;
    tag->lastDisplayScaled = INT32_MIN;
    tag->warnLevel = cfg.gauge.warnLevel;
    tag->dangerLevel = cfg.gauge.dangerLevel;
    tag->catalogue = WidgetHelpers::catalogueLimitsFor(cfg.signalId);
    tag->revFlash = cfg.gauge.revFlash;
    tag->lastLimiting = false;
    tag->convertibleUnit = UnitDisplay::convertibleUnitFor(cfg.signalId, cfg.gauge.suffix);
    WidgetHelpers::formatStalePlaceholder(
        tag->stalePlaceholder, sizeof(tag->stalePlaceholder),
        UnitDisplay::valueFor(cfg.gauge.maxValue, tag->convertibleUnit));
    WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, tag->stalePlaceholder);
    tag->signal = signalIdFromName(cfg.signalId);
    tag->cutLevel = Severity::Level::INFORMATION;
}

}

namespace {

lv_obj_t *buildArcs(lv_obj_t *cont, const CfgWidget &cfg, uint32_t inkRgb) {
    const int32_t diam = GaugeArcs::diameterFor(cfg);
    const uint8_t strokeW = GaugeArcs::strokeWidthFor(cfg);
    GaugeArcs::buildBackgroundTrack(cont, diam, strokeW);
    return GaugeArcs::buildValueFill(cont, diam, inkRgb, strokeW);
}

}

lv_obj_t *GaugeWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const GaugeArcModes modes = resolveArcModes(cfg);
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    lv_obj_t *fillArc = buildArcs(cont, cfg, textRgb);

    lv_obj_t *label = nullptr;
    lv_obj_t *kicker = nullptr;
    buildValueCluster(cont, cfg, label, kicker);

    uint8_t valueFontPx = kValueFontSizeUnits;
    const lv_font_t *valueFont = resolveValueFont(cfg, valueFontPx);
    WidgetHelpers::reportValueOverflow(cfg, valueFont, WidgetHelpers::valueTrackingPx(valueFontPx));

    const bool primaryTier = WidgetHelpers::scaledBox(cfg).h >= kValueFontHeightPrimary;
    const uint8_t rulePx = primaryTier ? Severity::kRulePrimaryPx : Severity::kRuleSecondaryPx;
    lv_obj_t *topRule =
        WidgetHelpers::makeTopRule(cont, rulePx, Severity::baseRuleRgbFor(rulePx, textRgb));

    WidgetTagPool::Slot<GaugeTag> tagSlot;
    GaugeTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "GAUGE", cont);
    if (!tag)
        return nullptr;
    const GaugeBuildState built = {label,  fillArc,           topRule,        kicker,
                                   rulePx, modes.dangerAngle, modes.hasDanger};
    initGaugeTag(tag, cfg, built, textRgb);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GaugeTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

static void renderStale(GaugeTag *tag) {
    if (tag->lastValid) {
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, tag->stalePlaceholder);
        tag->lastValue = NAN;
        tag->lastValid = false;
        tag->lastDisplayScaled = INT32_MIN;
    }
    const uint32_t staleRgb = ThemeManager::getStaleTextColor();
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, staleRgb);
    if (tag->fillArc && tag->lastAngle != 0u) {
        WidgetHelpers::setFillImmediate(tag->fillArc, GaugeArcs::angleAnimCb, 0);
        tag->lastAngle = 0u;
    }
    Severity::repaint(tag->severity, Severity::Level::INFORMATION);
}

static bool rememberReading(GaugeTag *tag, float value, bool limiting, Severity::Level cutLevel) {
    if (tag->lastValid && value == tag->lastValue && limiting == tag->lastLimiting &&
        cutLevel == tag->cutLevel)
        return false;
    tag->lastValue = value;
    tag->lastValid = true;
    tag->lastLimiting = limiting;
    tag->cutLevel = cutLevel;
    return true;
}

static void updateFillArc(GaugeTag *tag, float value, uint32_t levelRgb) {
    if (!tag->fillArc)
        return;
    const uint16_t angle = GaugeArcs::valueToAngle(value, tag->minValue, tag->maxValue);
    if (angle != tag->lastAngle) {
        WidgetHelpers::animateFill(tag->fillArc, GaugeArcs::angleAnimCb,
                                   lv_arc_get_angle_end(tag->fillArc), angle);
        tag->lastAngle = angle;
    }
    WidgetStyles::setArcColorIfChanged(tag->fillArc, tag->lastFillRgb, levelRgb, LV_PART_INDICATOR);
}

static void updateValueText(GaugeTag *tag, const CfgWidget &cfg, float value) {
    const float shown = UnitDisplay::valueFor(value, tag->convertibleUnit);
    const int32_t displayScaled = scaleForDisplay(shown, cfg.gauge.decimalPlaces);
    if (displayScaled == tag->lastDisplayScaled)
        return;
    char buf[24];
    WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix, cfg.gauge.decimalPlaces, shown,
                               nullptr);
    WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
    tag->lastDisplayScaled = displayScaled;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    if (!valid) {
        renderStale(tag);
        return;
    }

    const bool limiting = tag->revFlash && RevLimitFlash::isEngaged();
    const Severity::Level cutLevel = CutBand::levelFor(tag->signal);
    if (!rememberReading(tag, value, limiting, cutLevel))
        return;
    const Severity::Level level =
        Severity::strongerOf(gaugeLevelFor(tag, value, cfg.gauge.dangerBelow, limiting), cutLevel);
    const uint32_t levelRgb = gaugeInkFor(tag, level);
    WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, levelRgb);
    updateFillArc(tag, value, levelRgb);
    Severity::repaint(tag->severity, level);
    updateValueText(tag, cfg, value);
}
