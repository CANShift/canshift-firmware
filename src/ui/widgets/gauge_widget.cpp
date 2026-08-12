#include "gauge_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
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

static constexpr const char *kStalePlaceholder = "- -";

static constexpr int16_t kValueFontHeightPrimary = 90;
static constexpr int16_t kValueFontHeightSecondary = 40;
static constexpr uint8_t kValueFontSizePrimary = 48;
static constexpr uint8_t kValueFontSizeSecondary = 17;
static constexpr uint8_t kValueFontSizeUnits = 10;

static constexpr float kArcSweep = 270.0f;
static constexpr uint16_t kArcSweepInt = 270;
static constexpr uint16_t kArcRotation = 135;

static constexpr int32_t kMinArcDiam = 40;
static constexpr int32_t kArcContainerPadding = 4;
static constexpr int16_t kArcYShift = 2;

static uint8_t computeArcStrokeWidth(const CfgWidget &cfg) {
    const float r = std::min(static_cast<float>(cfg.layout.w) * 0.48f,
                             static_cast<float>(cfg.layout.h) * 0.49f);
    const float w = r * 0.30f;
    return static_cast<uint8_t>(w < 5.0f ? 5.0f : w);
}

static uint16_t valueToAngle(float value, float minVal, float maxVal) {
    return static_cast<uint16_t>(WidgetHelpers::clampPct(value, minVal, maxVal) * kArcSweep);
}

static lv_obj_t *createSectorArc(lv_obj_t *parent, int32_t diam, uint16_t startAngle,
                                 uint16_t endAngle, uint32_t colorRgb, uint8_t arcWidth) {
    if (startAngle >= endAngle)
        return nullptr;

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, kArcYShift);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, kArcRotation);
    lv_arc_set_bg_angles(arc, startAngle, endAngle);
    lv_arc_set_angles(arc, 0, 0);

    lv_obj_set_style_arc_color(arc, lv_color_hex(colorRgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

static lv_obj_t *createValueArc(lv_obj_t *parent, int32_t diam, uint8_t indicatorWidth) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, kArcYShift);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, kArcRotation);
    lv_arc_set_bg_angles(arc, 0, kArcSweepInt);
    lv_arc_set_angles(arc, 0, 0);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFFu), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, indicatorWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

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
    lv_obj_t *topRule;
    lv_obj_t *kicker;
    float minValue;
    float maxValue;
    float lastValue;
    float alertThreshold;
    float revFlashThreshold;
    uint16_t dangerAngle;
    bool hasDanger;
    bool lastValid;
    uint32_t inkRgb;
    float dangerLevel;
    AlertFlash::State alert;
    uint32_t lastLabelRgb;
    uint32_t lastFillRgb;
    uint32_t ruleBaseRgb;
    uint32_t ruleLastRgb;
    uint32_t kickerLastRgb;
    bool lastDangerActive;
    uint16_t lastAngle;
    int32_t lastDisplayScaled;
};

static void applyDangerChrome(GaugeTag *tag, bool danger) {
    WidgetHelpers::setRuleColorIfChanged(tag->topRule, tag->ruleLastRgb,
                                         danger ? WidgetHelpers::kZoneDangerRgb : tag->ruleBaseRgb);
    if (danger == tag->lastDangerActive)
        return;
    tag->lastDangerActive = danger;
    if (!tag->kicker)
        return;
    const uint32_t kickerRgb = danger ? WidgetHelpers::kZoneDangerRgb : ThemeManager::dimColor();
    if (tag->kickerLastRgb != kickerRgb) {
        lv_obj_set_style_text_color(tag->kicker, lv_color_hex(kickerRgb), 0);
        tag->kickerLastRgb = kickerRgb;
    }
}

inline float effectiveAlertThreshold(const GaugeTag &tag) {
    const bool hasAlert = !std::isnan(tag.alertThreshold);
    const bool hasRev = !std::isnan(tag.revFlashThreshold);
    if (hasAlert && hasRev)
        return tag.alertThreshold < tag.revFlashThreshold ? tag.alertThreshold
                                                          : tag.revFlashThreshold;
    if (hasAlert)
        return tag.alertThreshold;
    if (hasRev)
        return tag.revFlashThreshold;
    return NAN;
}

static int32_t computeArcDiameter(const CfgWidget &cfg) {
    int32_t diam =
        (cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h) - kArcContainerPadding;
    if (diam < kMinArcDiam)
        diam = kMinArcDiam;
    return diam;
}

static void buildBackgroundTracks(lv_obj_t *cont, int32_t diam, uint8_t strokeW) {
    createSectorArc(cont, diam, 0, kArcSweepInt, ThemeManager::trackColor(), strokeW);
}

static lv_obj_t *buildValueFillArc(lv_obj_t *cont, int32_t diam, uint32_t inkRgb, uint8_t strokeW) {
    lv_obj_t *fillArc = createValueArc(cont, diam, strokeW);
    lv_arc_set_angles(fillArc, 0, 0);
    lv_obj_set_style_arc_color(fillArc, lv_color_hex(inkRgb), LV_PART_INDICATOR);
    return fillArc;
}

static const lv_font_t *resolveValueFont(const CfgWidget &cfg, uint8_t &intFontSizeOut) {
    if (cfg.gauge.big > 0) {
        intFontSizeOut = WidgetHelpers::deviceFontPxForBig(cfg.gauge.big);
        return FontManager::value(intFontSizeOut);
    }
    const int16_t h = cfg.layout.h;
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

static constexpr int16_t kValueRowYOffset = kArcYShift;
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
    lv_label_set_text(label, kStalePlaceholder);
    return label;
}

static float resolveRevFlashThreshold(const CfgWidget &cfg) {
    if (!cfg.gauge.revFlash)
        return NAN;
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || dash.revLimitRpm <= 0.0f)
        return NAN;
    return dash.revLimitRpm - 1.0f;
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
    m.dangerAngle = m.hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;
    return m;
}

struct GaugeBuildState {
    lv_obj_t *label;
    lv_obj_t *fillArc;
    lv_obj_t *topRule;
    lv_obj_t *kicker;
    uint32_t ruleBaseRgb;
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
    outKicker = WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId,
                                                      WidgetLabelOverlay::HeaderPos::TOP_LEFT);
}

static void initGaugeTag(GaugeTag *tag, const CfgWidget &cfg, const GaugeBuildState &built,
                         uint32_t inkRgb) {
    tag->valueLabel = built.label;
    tag->fillArc = built.fillArc;
    tag->topRule = built.topRule;
    tag->kicker = built.kicker;
    tag->ruleBaseRgb = built.ruleBaseRgb;
    tag->ruleLastRgb = built.ruleBaseRgb;
    tag->kickerLastRgb = ThemeManager::dimColor();
    tag->lastDangerActive = false;
    tag->minValue = cfg.gauge.minValue;
    tag->maxValue = cfg.gauge.maxValue;
    tag->lastValue = NAN;
    tag->alertThreshold = cfg.gauge.alertThreshold;
    tag->dangerAngle = built.dangerAngle;
    tag->hasDanger = built.hasDanger;
    tag->inkRgb = inkRgb;
    tag->lastValid = false;
    tag->lastLabelRgb = ThemeManager::getStaleTextColor();
    tag->lastFillRgb = 0xFFFFFFFFu;
    tag->lastAngle = 0xFFFFu;
    tag->lastDisplayScaled = INT32_MIN;
    tag->dangerLevel = cfg.gauge.dangerLevel;
    tag->revFlashThreshold = resolveRevFlashThreshold(cfg);
}

static void attachAlertFlash(GaugeTag *tag, lv_obj_t *cont, const CfgWidget &cfg) {
    AlertFlash::attach(tag->alert, cont);
    const uint32_t valueRgb = tag->inkRgb;
    AlertFlash::watchLabel(tag->alert, tag->valueLabel, valueRgb);
}

} // namespace

lv_obj_t *GaugeWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const int32_t diam = computeArcDiameter(cfg);
    const GaugeArcModes modes = resolveArcModes(cfg);
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    const uint8_t strokeW = computeArcStrokeWidth(cfg);
    buildBackgroundTracks(cont, diam, strokeW);
    lv_obj_t *fillArc = buildValueFillArc(cont, diam, textRgb, strokeW);

    lv_obj_t *label = nullptr;
    lv_obj_t *kicker = nullptr;
    buildValueCluster(cont, cfg, label, kicker);

    const bool primaryTier = cfg.layout.h >= kValueFontHeightPrimary;
    const uint32_t ruleRgb = primaryTier ? textRgb : ThemeManager::trackColor();
    lv_obj_t *topRule = WidgetHelpers::makeTopRule(
        cont, primaryTier ? WidgetHelpers::kRulePrimaryPx : WidgetHelpers::kRuleSecondaryPx,
        ruleRgb);

    WidgetTagPool::Slot<GaugeTag> tagSlot;
    GaugeTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "GAUGE", cont);
    if (!tag)
        return nullptr;
    const GaugeBuildState built = {label,   fillArc,           topRule,        kicker,
                                   ruleRgb, modes.dangerAngle, modes.hasDanger};
    initGaugeTag(tag, cfg, built, textRgb);
    attachAlertFlash(tag, cont, cfg);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GaugeTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

static void renderStale(GaugeTag *tag) {
    if (tag->lastValid) {
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, kStalePlaceholder);
        tag->lastValue = NAN;
        tag->lastValid = false;
        tag->lastDisplayScaled = INT32_MIN;
    }
    if (!tag->alert.active) {
        const uint32_t staleRgb = ThemeManager::getStaleTextColor();
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, staleRgb);
    }
    if (tag->fillArc && tag->lastAngle != 0u) {
        lv_arc_set_angles(tag->fillArc, 0, 0);
        tag->lastAngle = 0u;
    }
    applyDangerChrome(tag, false);
    AlertFlash::update(tag->alert, 0.0f, effectiveAlertThreshold(*tag));
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const float displayValue = valid ? value : 0.0f;

    if (!valid) {
        renderStale(tag);
        return;
    }

    if (tag->lastValid && value == tag->lastValue) {
        AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
        return;
    }
    tag->lastValue = value;
    tag->lastValid = true;

    if (!tag->alert.active) {
        const bool inDanger = tag->hasDanger && value >= cfg.gauge.dangerLevel;
        const uint32_t labelColor = inDanger ? cfg.style.criticalColor.rgb : tag->inkRgb;
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, labelColor);
    }

    if (tag->fillArc) {
        const uint16_t angle = valueToAngle(value, tag->minValue, tag->maxValue);
        if (angle != tag->lastAngle) {
            lv_arc_set_angles(tag->fillArc, 0, angle);
            tag->lastAngle = angle;
        }

        const bool fillDanger = tag->hasDanger && value >= cfg.gauge.dangerLevel;
        const uint32_t fillColor = fillDanger ? cfg.style.criticalColor.rgb : tag->inkRgb;
        WidgetStyles::setArcColorIfChanged(tag->fillArc, tag->lastFillRgb, fillColor,
                                           LV_PART_INDICATOR);
    }

    applyDangerChrome(tag, tag->hasDanger && value >= cfg.gauge.dangerLevel);

    const int32_t displayScaled = scaleForDisplay(value, cfg.gauge.decimalPlaces);
    if (displayScaled != tag->lastDisplayScaled) {
        char buf[24];
        WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix, cfg.gauge.decimalPlaces,
                                   value, nullptr);
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
        tag->lastDisplayScaled = displayScaled;
    }

    AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
}
