// gauge_widget.cpp — Arc-based gauge widget with colored zone sectors

#include "gauge_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/sensor_color_ramp.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "config/config_loader.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Palette for the colored arc sectors. Zone tints come from the shared
// helper palette so bar + gauge stay in lockstep.
static constexpr uint32_t kColorBgDim = 0x222222; // Dark grey — no-threshold bg track
static constexpr uint32_t kColorValue = 0xFFFFFF; // White — value indicator needle
// Gradient base track (issue #175) — slightly lighter than the no-threshold
// track so the value arc reads cleanly on top of it.
static constexpr uint32_t kColorGradientBg = 0x2A2A2A;

// Arc sweep constants (matches rotation=140, bg_angles=0..280)
static constexpr float kArcSweep = 280.0f;

// Linear interpolation between two RGB colours, channel-wise.
// Returns a 0x00RRGGBB integer suitable for lv_color_hex.
static uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    auto channel = [](uint32_t c, int shift) {
        return static_cast<int>((c >> shift) & 0xFFu);
    };
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

// Map a [0,1] percentage to the green→orange→red gradient (issue #175).
// 0..0.5 lerps green→orange, 0.5..1.0 lerps orange→red. The pivot at 0.5
// keeps the orange "warning" colour visible at mid-range so drivers can read
// the gauge intuitively without configuring thresholds.
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

// Map a signal value to arc angle [0..280]
static uint16_t valueToAngle(float value, float minVal, float maxVal) {
    return static_cast<uint16_t>(WidgetHelpers::clampPct(value, minVal, maxVal) * kArcSweep);
}

// Create a background-only arc covering [startAngle..endAngle] in the given color.
// The indicator portion is fully transparent — only the track ring is visible.
static lv_obj_t *createSectorArc(lv_obj_t *parent, int32_t diam, uint16_t startAngle,
                                 uint16_t endAngle, uint32_t colorRgb, uint8_t arcWidth) {
    if (startAngle >= endAngle)
        return nullptr;

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 140);
    lv_arc_set_bg_angles(arc, startAngle, endAngle);
    lv_arc_set_angles(arc, 0, 0); // No indicator

    // Background track: colored zone — square ends per dashboard styling.
    lv_obj_set_style_arc_color(arc, lv_color_hex(colorRgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    // Indicator: invisible
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

// Create the value indicator arc (transparent bg, bright indicator on top).
// Rendered last so it draws above the sector layers.
static lv_obj_t *createValueArc(lv_obj_t *parent, int32_t diam, uint8_t indicatorWidth) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, 140);
    lv_arc_set_bg_angles(arc, 0, 280);
    lv_arc_set_angles(arc, 0, 0);

    // Background: transparent (sector arcs show through)
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    // Indicator: bright white needle — square ends.
    lv_obj_set_style_arc_color(arc, lv_color_hex(kColorValue), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, indicatorWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

// Tag structure stored in LVGL user data
struct GaugeTag {
    lv_obj_t *valueLabel;
    lv_obj_t *unitLabel;
    lv_obj_t *fillArc;       // Gradient mode only — value arc whose colour is
                             // recomputed each tick. nullptr in zones mode.
    float minValue;
    float maxValue;
    float lastValue;
    float alertThreshold;    // NaN = disabled (issue #133)
    float revFlashThreshold; // NaN = revFlash disabled or no revLimitRpm (issue #263).
                             // Stored as `revLimitRpm - 1.0f` so AlertFlash's
                             // strict `value > threshold` test triggers at the
                             // first RPM sample that reaches the limit.
    // Threshold angles (0 = not set)
    uint16_t warnAngle;
    uint16_t dangerAngle;
    bool hasWarning;
    bool hasDanger;
    bool gradientMode; // True when arcFillStyle == GRADIENT (issue #175)
    bool lastValid; // Tracks the last (value, valid) pair so the invalid
                    // branch can skip its lv_label_set_text reallocation
                    // when state hasn't flipped (issue #236).
    const CfgColorRamp *ramp; // Active color ramp (issue #430). nullptr → static path.
    AlertFlash::State alert;
    // Last colours pushed to LVGL — used by the per-frame write guards. Init
    // to 0xFFFFFFFFu so the first update() always paints (alpha bits never
    // appear in a 0x00RRGGBB target so the sentinel is unique).
    uint32_t lastLabelRgb;
    uint32_t lastFillRgb;
};

// Combine the per-signal alertThreshold (issue #133) with the revFlash trigger
// (issue #263) into a single value that AlertFlash::update() can consume. The
// lower of the two thresholds wins so either condition pulses the overlay.
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

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GaugeWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    // Container
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    // Arc diameter: smallest of w/h, minus padding
    int32_t diam = (cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h) - 8;
    if (diam < 40)
        diam = 40;

    // Determine which threshold zones apply. The shared helper resolves the
    // band positions in pct-domain; the gauge maps them to arc angles below.
    // Convention: warningLevel > minValue means a real threshold is configured.
    const float minV = cfg.gauge.minValue;
    const float maxV = cfg.gauge.maxValue;
    const WidgetHelpers::ThresholdZones zones =
        WidgetHelpers::resolveZones(cfg.gauge.warningLevel, cfg.gauge.dangerLevel, minV, maxV);
    // The gauge applies an extra "warning < max" guard the shared helper does
    // not — preserve that here so behaviour is byte-identical.
    const bool hasWarning = zones.hasWarn && cfg.gauge.warningLevel < maxV;
    const bool hasDanger = hasWarning && zones.hasDanger &&
                           cfg.gauge.dangerLevel > cfg.gauge.warningLevel;

    const uint16_t warnAngle = hasWarning ? valueToAngle(cfg.gauge.warningLevel, minV, maxV) : 0;
    const uint16_t dangerAngle = hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;

    // Arc line widths — thick enough to read on the now-smaller (h=80)
    // dashboard arcs without overwhelming the value text in the centre.
    constexpr uint8_t kBgWidth = 14; // Sector track width
    constexpr uint8_t kIndWidth = 7; // Value indicator (thinner, sits inside)

    // ---------------------------------------------------------------------------
    // Layer 1-3: Static sector arcs (background track, colored zones)
    // Render order: first created = bottom, last created = top.
    // Gradient mode (issue #175) skips the zone tinting entirely — it draws a
    // single mid-grey base track and a coloured value arc on top.
    // ---------------------------------------------------------------------------

    const bool gradientMode = (cfg.gauge.arcFillStyle == CfgArcFillStyle::GRADIENT);
    lv_obj_t *fillArc = nullptr;

    if (gradientMode) {
        // Gray base track over the full sweep — the value arc tints over it.
        createSectorArc(cont, diam, 0, 280, kColorGradientBg, kBgWidth);
    } else if (!hasWarning) {
        // No thresholds — single dimmed background track (old behavior)
        createSectorArc(cont, diam, 0, 280, kColorBgDim, kBgWidth);
    } else if (!hasDanger) {
        // Two zones: green (0→warn) + orange (warn→280)
        createSectorArc(cont, diam, 0, warnAngle, WidgetHelpers::kZoneNormalRgb, kBgWidth);
        createSectorArc(cont, diam, warnAngle, 280, WidgetHelpers::kZoneWarningRgb, kBgWidth);
    } else {
        // Three zones: green (0→warn), orange (warn→danger), red (danger→280)
        createSectorArc(cont, diam, 0, warnAngle, WidgetHelpers::kZoneNormalRgb, kBgWidth);
        createSectorArc(cont, diam, warnAngle, dangerAngle, WidgetHelpers::kZoneWarningRgb,
                        kBgWidth);
        createSectorArc(cont, diam, dangerAngle, 280, WidgetHelpers::kZoneDangerRgb, kBgWidth);
    }

    // Gradient mode value arc (issue #175). Draws on top of the gray base.
    // The indicator sweep + colour are recomputed each tick in update().
    if (gradientMode) {
        fillArc = createValueArc(cont, diam, kBgWidth);
        lv_arc_set_angles(fillArc, 0, 0);
        // Replace the default white indicator colour with the gradient's
        // starting colour (green, 0 % of range).
        lv_obj_set_style_arc_color(fillArc, lv_color_hex(WidgetHelpers::kZoneNormalRgb),
                                   LV_PART_INDICATOR);
    }

    // The white indicator needle was dropped per user spec — the coloured
    // sector arcs + the centred numeric value carry the read on their own.

    // ---------------------------------------------------------------------------
    // Value label (centered inside the arc)
    // ---------------------------------------------------------------------------

    bool hasUnit = cfg.gauge.suffix[0] != '\0';
    const uint32_t textRgb = ThemeManager::getEffectiveTextColor();
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, hasUnit ? -8 : 0);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);

    const lv_font_t *font = FontManager::secondary(20);
    if (cfg.layout.h >= 100)
        font = FontManager::secondary(24);
    if (cfg.layout.h >= 130)
        font = FontManager::primary(32);
    lv_obj_set_style_text_font(label, font, 0);
    {
        // Initial readout: 0, formatted to the configured decimalPlaces with
        // optional prefix — matches the "no signal yet → show 0" rule.
        char initBuf[24];
        WidgetHelpers::formatValue(initBuf, sizeof(initBuf), cfg.gauge.prefix,
                                   cfg.gauge.decimalPlaces, 0.0f, nullptr);
        lv_label_set_text(label, initBuf);
    }

    // Unit label below value (optional)
    lv_obj_t *unitLabel = nullptr;
    if (hasUnit) {
        unitLabel = lv_label_create(cont);
        lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 12);
        lv_obj_set_style_text_color(unitLabel, lv_color_hex(textRgb & 0x888888), 0);
        lv_obj_set_style_text_font(unitLabel, FontManager::label(12), 0);
        lv_label_set_text(unitLabel, cfg.gauge.suffix);
    }

    // Optional widget label drawn at the configured corner.
    WidgetLabelOverlay::apply(cont, cfg.gauge.label, cfg.gauge.labelPosition, textRgb);

    // Allocate and attach tag
    GaugeTag *tag = new GaugeTag{};
    tag->valueLabel = label;
    tag->unitLabel = unitLabel;
    tag->fillArc = fillArc;
    tag->minValue = minV;
    tag->maxValue = maxV;
    // NaN sentinel — guarantees the first update() runs through the paint
    // path even when the live value happens to be 0.0 (matches bar_widget).
    tag->lastValue = NAN;
    tag->alertThreshold = cfg.gauge.alertThreshold;
    tag->warnAngle = warnAngle;
    tag->dangerAngle = dangerAngle;
    tag->hasWarning = hasWarning;
    tag->hasDanger = hasDanger;
    tag->gradientMode = gradientMode;
    tag->lastValid = true;
    tag->lastLabelRgb = 0xFFFFFFFFu;
    tag->lastFillRgb = 0xFFFFFFFFu;

    // Resolve the active color ramp once (issue #430). Prefer the per-signal
    // ramp from signals.json; fall back to the sensor-name heuristic; null
    // means the legacy threshold-driven static colors are used unchanged.
    tag->ramp = WidgetHelpers::resolveSignalRamp(cfg.signalId);

    // revFlash (issue #263): when enabled, pulse the gauge red as soon as the
    // signal reaches the dashboard's rev-limit RPM. We snapshot the threshold
    // here so update() doesn't have to consult ConfigLoader on every tick. The
    // `- 1.0f` offset turns AlertFlash's strict `>` test into a `>=` for the
    // integer-stepped RPM signal.
    tag->revFlashThreshold = NAN;
    if (cfg.gauge.revFlash) {
        const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
        if (dash.loaded && dash.revLimitRpm > 0.0f) {
            tag->revFlashThreshold = dash.revLimitRpm - 1.0f;
        }
    }

    // Mount the alert overlay last so it sits on top of arcs and labels.
    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, label, textRgb);
    if (unitLabel) {
        AlertFlash::watchLabel(tag->alert, unitLabel, textRgb & 0x888888);
    }

    WidgetHelpers::attachTagDeleter(cont, tag);

    return cont;
}

void GaugeWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;

    GaugeTag *tag = static_cast<GaugeTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const float displayValue = valid ? value : 0.0f;

    if (!valid) {
        // No live signal → display 0 (formatted) instead of placeholder dashes.
        // Skip the realloc when we're already showing the invalid readout
        // (issue #236) — the formatted "0" buffer is identical every tick.
        if (tag->lastValid || !std::isnan(tag->lastValue) || tag->lastValue != 0.0f) {
            char buf[24];
            WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix,
                                       cfg.gauge.decimalPlaces, 0.0f, nullptr);
            WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);
            tag->lastValue = 0.0f;
            tag->lastValid = false;
        }
        if (!tag->alert.active) {
            WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb,
                                                ThemeManager::getEffectiveTextColor());
        }
        // Gradient mode (issue #175): collapse the value arc to zero on
        // invalid signals so the gray base track is visible — matches the
        // "show 0" rule above.
        if (tag->gradientMode && tag->fillArc) {
            lv_arc_set_angles(tag->fillArc, 0, 0);
        }
        AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
        return;
    }

    // Only redraw if value (or valid flag) changed to avoid LVGL refresh churn.
    if (tag->lastValid && value == tag->lastValue) {
        AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
        return;
    }
    tag->lastValue = value;
    tag->lastValid = true;

    // Tint the value label — skip when AlertFlash owns the colour. When a
    // ramp is configured (issue #430), the ramp drives the colour at the
    // live value. Otherwise fall back to the legacy warn/danger zone tints.
    if (!tag->alert.active) {
        uint32_t labelColor = ThemeManager::getEffectiveTextColor();
        if (tag->ramp) {
            labelColor = colorAtValue(*tag->ramp, value);
        } else if (tag->hasDanger && value >= cfg.gauge.dangerLevel) {
            labelColor = WidgetHelpers::kZoneDangerRgb;
        } else if (tag->hasWarning && value >= cfg.gauge.warningLevel) {
            labelColor = WidgetHelpers::kZoneWarningRgb;
        }
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, labelColor);
    }

    // Gradient mode: redraw the value arc with an interpolated colour and a
    // sweep proportional to (value - min) / (max - min). The ramp wins when
    // configured; otherwise fall back to the built-in green→orange→red lerp.
    if (tag->gradientMode && tag->fillArc) {
        const uint16_t angle = valueToAngle(value, tag->minValue, tag->maxValue);
        lv_arc_set_angles(tag->fillArc, 0, angle);
        uint32_t fillColor;
        if (tag->ramp) {
            fillColor = colorAtValue(*tag->ramp, value);
        } else {
            const float range = tag->maxValue - tag->minValue;
            const float pct = range > 0.0f ? (value - tag->minValue) / range : 0.0f;
            fillColor = interpolateGreenOrangeRed(pct);
        }
        WidgetStyles::setArcColorIfChanged(tag->fillArc, tag->lastFillRgb, fillColor,
                                           LV_PART_INDICATOR);
    }

    // Update numeric label (prefix + value formatted to decimalPlaces). Even
    // with a different float value, formatting may collapse to the same string
    // (e.g. 78.001 vs 78.004 at 0 dp) — strcmp guard avoids the realloc.
    char buf[24];
    WidgetHelpers::formatValue(buf, sizeof(buf), cfg.gauge.prefix, cfg.gauge.decimalPlaces, value,
                               nullptr);
    WidgetHelpers::setLabelTextIfChanged(tag->valueLabel, buf);

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    // The effective threshold merges alertThreshold (#133) with revFlash (#263);
    // either trigger pulses the same overlay.
    AlertFlash::update(tag->alert, displayValue, effectiveAlertThreshold(*tag));
}
