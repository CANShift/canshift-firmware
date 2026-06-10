#include "gauge_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/sensor_color_ramp.h"
#include "ui/sensor_palette.h"
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

static constexpr uint32_t kColorBgDim = 0x222222;
static constexpr uint32_t kColorGradientBg = 0x2A2A2A;

// Matches Studio's gauge-math.ts (SoT per #1183).
static constexpr float kArcSweep = 270.0f;
static constexpr uint16_t kArcSweepInt = 270;
static constexpr uint16_t kArcRotation = 135;

static constexpr int32_t kMinArcDiam = 40;
static constexpr int32_t kArcContainerPadding = 8;

// Mirrors GaugeArc.tsx:69 `Math.max(5, r * 0.24)` with r = Math.min(w*0.45, h*0.46).
static uint8_t computeArcStrokeWidth(const CfgWidget &cfg) {
    const float r = std::min(static_cast<float>(cfg.layout.w) * 0.45f,
                             static_cast<float>(cfg.layout.h) * 0.46f);
    const float w = r * 0.24f;
    return static_cast<uint8_t>(w < 5.0f ? 5.0f : w);
}

static uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    auto channel = [](uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFFu); };
    const int ar = channel(a, 16);
    const int ag = channel(a, 8);
    const int ab = channel(a, 0);
    const int br = channel(b, 16);
    const int bg = channel(b, 8);
    const int bb = channel(b, 0);
    auto mix = [t](int x, int y) {
        const float v = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
        return static_cast<uint32_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    };
    const uint32_t r = mix(ar, br);
    const uint32_t g = mix(ag, bg);
    const uint32_t bch = mix(ab, bb);
    return (r << 16) | (g << 8) | bch;
}

static uint32_t interpolateGreenOrangeRed(float pct) {
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 1.0f)
        pct = 1.0f;
    if (pct <= 0.5f) {
        return lerpRgb(WidgetHelpers::kZoneNormalRgb, WidgetHelpers::kZoneWarningRgb, pct * 2.0f);
    }
    return lerpRgb(WidgetHelpers::kZoneWarningRgb, WidgetHelpers::kZoneDangerRgb,
                   (pct - 0.5f) * 2.0f);
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
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
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
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
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

// Mirrors label_widget.cpp's helper. Fractional output includes the dot.
static void splitDecimal(const char *in, char *intOut, size_t intCap, char *fracOut,
                         size_t fracCap) {
    const char *dot = strchr(in, '.');
    if (!dot) {
        strlcpy(intOut, in, intCap);
        if (fracCap > 0)
            fracOut[0] = '\0';
        return;
    }
    const size_t intLen = static_cast<size_t>(dot - in);
    const size_t copyInt = intLen < intCap - 1 ? intLen : intCap - 1;
    memcpy(intOut, in, copyInt);
    intOut[copyInt] = '\0';
    strlcpy(fracOut, dot, fracCap);
}

struct GaugeTag {
    lv_obj_t *valueLabel;
    lv_obj_t *fracLabel;
    lv_obj_t *unitLabel;
    lv_obj_t *fillArc;
    float minValue;
    float maxValue;
    float lastValue;
    float alertThreshold;
    // Stored as revLimitRpm - 1.0f so AlertFlash's `>` test triggers at the limit (#263).
    float revFlashThreshold;
    uint16_t dangerAngle;
    bool hasDanger;
    bool gradientMode;
    bool lastValid;
    const CfgColorRamp *ramp;
    const SensorPaletteEntry *palette;
    float dangerLevel;
    AlertFlash::State alert;
    // 0xFFFFFFFFu sentinel — guarantees first paint (alpha never in 0x00RRGGBB).
    uint32_t lastLabelRgb;
    uint32_t lastFillRgb;
    // Caches the last pushed angle so EMA noise can't trigger redundant
    // lv_arc_set_angles invalidations (#1342). 0xFFFFu sentinel.
    uint16_t lastAngle;
};

// Lower of alertThreshold (#133) and revFlashThreshold (#263) wins.
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

// Palette + gradient modes use a lighter base so the value arc tints cleanly.
static void buildBackgroundTracks(lv_obj_t *cont, int32_t diam, bool paletteMode, bool gradientMode,
                                  uint8_t strokeW) {
    const uint32_t color = (paletteMode || gradientMode) ? kColorGradientBg : kColorBgDim;
    createSectorArc(cont, diam, 0, kArcSweepInt, color, strokeW);
}

static lv_obj_t *buildValueFillArc(lv_obj_t *cont, int32_t diam,
                                   const SensorPaletteEntry *paletteEntry, bool paletteMode,
                                   bool gradientMode, uint32_t primaryRgb, uint8_t strokeW) {
    lv_obj_t *fillArc = createValueArc(cont, diam, strokeW);
    lv_arc_set_angles(fillArc, 0, 0);
    const uint32_t startColor = paletteMode    ? paletteEntry->okColor
                                : gradientMode ? WidgetHelpers::kZoneNormalRgb
                                               : primaryRgb;
    lv_obj_set_style_arc_color(fillArc, lv_color_hex(startColor), LV_PART_INDICATOR);
    return fillArc;
}

// Pick the integer-tier font size by container height. Studio uses the
// continuous formula `max(11, min(r*0.55, h*0.3, 42))` with `r = min(w*0.45,
// h*0.46)` (GaugeArc.tsx:77); firmware can only emit baked tiers (20/24
// Bold, 32/48 Black — 28 Bold was dropped, see font_manager.cpp:46-49).
// Thresholds picked so each tier minimises the absolute pixel-size delta
// to Studio at typical widget heights — see PR body for the residual
// (h≈110: Studio 27 px Black, firmware 24 px Bold — 3 px size delta plus
// the Bold→Black weight drift the audit lists separately as `med`).
//
// TODO(#18): hard-coded against the v1 design canvas (320×240). When a
// second screen profile lands the thresholds must be scaled with
// `ScreenProfile::scaleYVal` and the baked sizes must follow.
static const lv_font_t *resolveValueFont(const CfgWidget &cfg, uint8_t &intFontSizeOut) {
    const int16_t h = cfg.layout.h;
    if (h >= 165) {
        intFontSizeOut = 48;
        return FontManager::primary(48);
    }
    if (h >= 125) {
        intFontSizeOut = 32;
        return FontManager::primary(32);
    }
    if (h >= 95) {
        intFontSizeOut = 24;
        return FontManager::secondary(24);
    }
    intFontSizeOut = 20;
    return FontManager::secondary(20);
}

// Negative Y nudge keeps value clear of bottom-anchored widget label (#1241).
static constexpr int16_t kValueRowYOffset = -8;
static constexpr int16_t kUnitLabelYOffset = 16;

static lv_obj_t *buildValueRow(lv_obj_t *cont) {
    lv_obj_t *valueRow = lv_obj_create(cont);
    lv_obj_set_size(valueRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(valueRow, LV_ALIGN_CENTER, 0, kValueRowYOffset);
    lv_obj_set_style_bg_opa(valueRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(valueRow, 2, LV_PART_MAIN);
    lv_obj_clear_flag(valueRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    return valueRow;
}

static lv_obj_t *buildValueLabel(lv_obj_t *valueRow, const lv_font_t *font, uint32_t textRgb,
                                 const CfgWidget &cfg) {
    lv_obj_t *label = lv_label_create(valueRow);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, font, 0);
    char initBuf[24];
    WidgetHelpers::formatValue(initBuf, sizeof(initBuf), cfg.gauge.prefix, 0, 0.0f, nullptr);
    lv_label_set_text(label, initBuf);
    return label;
}

static lv_obj_t *buildFracLabel(lv_obj_t *valueRow, const CfgWidget &cfg, uint8_t intFontSize,
                                uint32_t textRgb) {
    if (cfg.gauge.decimalPlaces == 0)
        return nullptr;
    lv_obj_t *fracLabel = lv_label_create(valueRow);
    if (!fracLabel)
        return nullptr;
    uint8_t fracSize = static_cast<uint8_t>((intFontSize * 7) / 10);
    if (fracSize < 12)
        fracSize = 12;
    const lv_font_t *fracFont =
        (fracSize >= 20) ? FontManager::secondary(fracSize) : FontManager::label(fracSize);
    lv_obj_set_style_text_color(fracLabel, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(fracLabel, fracFont, 0);
    lv_label_set_text(fracLabel, "");
    return fracLabel;
}

static lv_obj_t *buildUnitLabel(lv_obj_t *cont, const char *unitText, uint32_t textRgb) {
    if (unitText[0] == '\0')
        return nullptr;
    lv_obj_t *unitLabel = lv_label_create(cont);
    lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, kUnitLabelYOffset);
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(textRgb & 0x888888), 0);
    lv_obj_set_style_text_font(unitLabel, FontManager::label(12), 0);
    lv_label_set_text(unitLabel, unitText);
    return unitLabel;
}

// `- 1.0f` makes AlertFlash's strict `>` test fire on the limit RPM (#263).
static float resolveRevFlashThreshold(const CfgWidget &cfg) {
    if (!cfg.gauge.revFlash)
        return NAN;
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || dash.revLimitRpm <= 0.0f)
        return NAN;
    return dash.revLimitRpm - 1.0f;
}

struct GaugeArcModes {
    const SensorPaletteEntry *paletteEntry;
    uint16_t dangerAngle;
    bool hasDanger;
    bool gradientMode;
    bool paletteMode;
};

static GaugeArcModes resolveArcModes(const CfgWidget &cfg) {
    GaugeArcModes m{};
    const float minV = cfg.gauge.minValue;
    const float maxV = cfg.gauge.maxValue;
    // Real danger threshold requires dangerLevel strictly inside (min, max) (#965).
    m.hasDanger = !std::isnan(cfg.gauge.dangerLevel) && cfg.gauge.dangerLevel > minV &&
                  cfg.gauge.dangerLevel < maxV;
    m.dangerAngle = m.hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;
    // Palette wins over zone tinting when iconName matches (#954).
    m.paletteEntry = SensorPalette::lookup(cfg.gauge.iconName);
    m.paletteMode = m.paletteEntry != nullptr;
    m.gradientMode = (cfg.gauge.arcFillStyle == CfgArcFillStyle::GRADIENT);
    return m;
}

struct GaugeBuildState {
    lv_obj_t *label;
    lv_obj_t *fracLabel;
    lv_obj_t *unitLabel;
    lv_obj_t *fillArc;
    const SensorPaletteEntry *paletteEntry;
    uint16_t dangerAngle;
    bool hasDanger;
    bool gradientMode;
};

static void buildValueCluster(lv_obj_t *cont, const CfgWidget &cfg, uint32_t textRgb,
                              lv_obj_t *&outLabel, lv_obj_t *&outFrac, lv_obj_t *&outUnit) {
    const char *unitText = WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.gauge.suffix);
    uint8_t intFontSize = 20;
    const lv_font_t *font = resolveValueFont(cfg, intFontSize);
    lv_obj_t *valueRow = buildValueRow(cont);
    const uint32_t valueRgb = cfg.style.primaryColor.rgb;
    outLabel = buildValueLabel(valueRow, font, valueRgb, cfg);
    outFrac = buildFracLabel(valueRow, cfg, intFontSize, valueRgb);
    outUnit = buildUnitLabel(cont, unitText, textRgb);
    // Auto signal header is the only label path on arc gauges (#1244).
    WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId,
                                          WidgetLabelOverlay::HeaderPos::BOTTOM_LEFT);
    (void)textRgb;
}

// Palette wins over per-signal ramp wins over legacy static colors (#430).
static void initGaugeTag(GaugeTag *tag, const CfgWidget &cfg, const GaugeBuildState &built) {
    tag->valueLabel = built.label;
    tag->fracLabel = built.fracLabel;
    tag->unitLabel = built.unitLabel;
    tag->fillArc = built.fillArc;
    tag->minValue = cfg.gauge.minValue;
    tag->maxValue = cfg.gauge.maxValue;
    // NaN sentinel forces a first paint even when value happens to be 0.0.
    tag->lastValue = NAN;
    tag->alertThreshold = cfg.gauge.alertThreshold;
    tag->dangerAngle = built.dangerAngle;
    tag->hasDanger = built.hasDanger;
    tag->gradientMode = built.gradientMode;
    tag->lastValid = true;
    tag->lastLabelRgb = 0xFFFFFFFFu;
    tag->lastFillRgb = 0xFFFFFFFFu;
    tag->lastAngle = 0xFFFFu;
    tag->palette = built.paletteEntry;
    tag->dangerLevel = cfg.gauge.dangerLevel;
    tag->ramp = built.paletteEntry ? nullptr : WidgetHelpers::resolveSignalRamp(cfg.signalId);
    tag->revFlashThreshold = resolveRevFlashThreshold(cfg);
}

// Mount last so it sits above arcs + labels. Tracked labels repaint in lockstep.
static void attachAlertFlash(GaugeTag *tag, lv_obj_t *cont, const CfgWidget &cfg,
                             uint32_t textRgb) {
    AlertFlash::attach(tag->alert, cont);
    const uint32_t valueRgb = cfg.style.primaryColor.rgb;
    AlertFlash::watchLabel(tag->alert, tag->valueLabel, valueRgb);
    if (tag->fracLabel) {
        AlertFlash::watchLabel(tag->alert, tag->fracLabel, valueRgb);
    }
    if (tag->unitLabel) {
        AlertFlash::watchLabel(tag->alert, tag->unitLabel, textRgb & 0x888888);
    }
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
    buildBackgroundTracks(cont, diam, modes.paletteMode, modes.gradientMode, strokeW);
    lv_obj_t *fillArc = buildValueFillArc(cont, diam, modes.paletteEntry, modes.paletteMode,
                                          modes.gradientMode, cfg.style.primaryColor.rgb, strokeW);

    lv_obj_t *label = nullptr;
    lv_obj_t *fracLabel = nullptr;
    lv_obj_t *unitLabel = nullptr;
    buildValueCluster(cont, cfg, textRgb, label, fracLabel, unitLabel);

    // RAII slot guard (#1207).
    WidgetTagPool::Slot<GaugeTag> tagSlot;
    GaugeTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("GAUGE", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    const GaugeBuildState built = {label,           fracLabel,          unitLabel,
                                   fillArc,         modes.paletteEntry, modes.dangerAngle,
                                   modes.hasDanger, modes.gradientMode};
    initGaugeTag(tag, cfg, built);
    attachAlertFlash(tag, cont, cfg, textRgb);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GaugeTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

void GaugeWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    if (tag->unitLabel) {
        lv_obj_set_style_text_color(tag->unitLabel, lv_color_hex(textRgb & 0x888888u), 0);
    }
    // Invalidate so the next update() repaints across a theme flip.
    tag->lastLabelRgb = 0xFFFFFFFFu;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const float displayValue = valid ? value : 0.0f;

    if (!valid) {
        // No signal → "0" placeholder; skip realloc when already shown (#236).
        if (tag->lastValid || !std::isnan(tag->lastValue) || tag->lastValue != 0.0f) {
            char buf[24];
            WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix, cfg.gauge.decimalPlaces,
                                       0.0f, nullptr);
            if (tag->fracLabel) {
                char intPart[16];
                char fracPart[12];
                splitDecimal(buf, intPart, sizeof(intPart), fracPart, sizeof(fracPart));
                WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, intPart);
                WidgetHelpers::setLabelTextIfChanged(tag->fracLabel, fracPart);
            } else {
                WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
            }
            tag->lastValue = 0.0f;
            tag->lastValid = false;
        }
        if (!tag->alert.active) {
            WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb,
                                                cfg.style.primaryColor.rgb);
        }
        // Cache guarded so sustained-invalid doesn't dirty the arc each tick (#1342).
        if (tag->fillArc && tag->lastAngle != 0u) {
            lv_arc_set_angles(tag->fillArc, 0, 0);
            tag->lastAngle = 0u;
        }
        AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
        return;
    }

    if (tag->lastValid && value == tag->lastValue) {
        AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
        return;
    }
    tag->lastValue = value;
    tag->lastValid = true;

    // Cache the palette/ramp lookup — label + arc previously scanned twice (#1342).
    uint32_t sharedColor = 0;
    bool hasSharedColor = false;
    if (tag->palette) {
        sharedColor = SensorPalette::fillColor(cfg.gauge.iconName, value, tag->dangerLevel);
        hasSharedColor = true;
    } else if (tag->ramp) {
        sharedColor = colorAtValue(*tag->ramp, value);
        hasSharedColor = true;
    }

    // Skip when AlertFlash owns the colour. No-palette/no-ramp matches
    // Studio's GaugeArc.tsx: primaryColor below danger, criticalColor above.
    if (!tag->alert.active) {
        uint32_t labelColor;
        if (hasSharedColor) {
            labelColor = sharedColor;
        } else if (tag->hasDanger && value >= cfg.gauge.dangerLevel) {
            labelColor = cfg.style.criticalColor.rgb;
        } else {
            labelColor = cfg.style.primaryColor.rgb;
        }
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, labelColor);
        if (tag->fracLabel) {
            uint32_t fracLast = tag->lastLabelRgb;
            WidgetStyles::setTextColorIfChanged(tag->fracLabel, fracLast, labelColor);
        }
    }

    // palette > ramp > gradient > two-zone > white (#965).
    if (tag->fillArc) {
        const uint16_t angle = valueToAngle(value, tag->minValue, tag->maxValue);
        // EMA noise rarely flips the uint16_t — cache skips redundant invalidations (#1342).
        if (angle != tag->lastAngle) {
            lv_arc_set_angles(tag->fillArc, 0, angle);
            tag->lastAngle = angle;
        }

        uint32_t fillColor;
        if (hasSharedColor) {
            fillColor = sharedColor;
        } else if (tag->gradientMode) {
            const float range = tag->maxValue - tag->minValue;
            const float pct = range > 0.0f ? (value - tag->minValue) / range : 0.0f;
            fillColor = interpolateGreenOrangeRed(pct);
        } else if (tag->hasDanger) {
            fillColor = value >= cfg.gauge.dangerLevel ? WidgetHelpers::kZoneDangerRgb
                                                       : WidgetHelpers::kZoneNormalRgb;
        } else {
            fillColor = 0xFFFFFFu;
        }
        WidgetStyles::setArcColorIfChanged(tag->fillArc, tag->lastFillRgb, fillColor,
                                           LV_PART_INDICATOR);
    }

    // strcmp guard — different float can collapse to same string after rounding.
    char buf[24];
    WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix, cfg.gauge.decimalPlaces, value,
                               nullptr);
    if (tag->fracLabel) {
        char intPart[16];
        char fracPart[12];
        splitDecimal(buf, intPart, sizeof(intPart), fracPart, sizeof(fracPart));
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, intPart);
        WidgetHelpers::setLabelTextIfChanged(tag->fracLabel, fracPart);
    } else {
        WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
    }

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    // The effective threshold merges alertThreshold (#133) with revFlash (#263);
    // either trigger pulses the same overlay.
    AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
}
