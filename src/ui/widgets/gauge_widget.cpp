// gauge_widget.cpp — Arc-based gauge widget with colored zone sectors

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

// Arc line widths — thick enough to read on the now-smaller (h=80)
// dashboard arcs without overwhelming the value text in the centre.
static constexpr uint8_t kBgWidth = 14; // Sector track width
static constexpr uint8_t kIndWidth = 7; // Value indicator (thinner, sits inside)

// Minimum arc diameter clamp — protects against degenerate gauge configs.
static constexpr int32_t kMinArcDiam = 40;
// Padding subtracted from min(w, h) when sizing the arc inside its container.
static constexpr int32_t kArcContainerPadding = 8;

// Linear interpolation between two RGB colours, channel-wise.
// Returns a 0x00RRGGBB integer suitable for lv_color_hex.
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

// Split a formatted numeric string at the first '.' into integer and
// fractional parts. The fractional buffer includes the dot itself (".3",
// ".09") so concatenating int+frac visually reads as the original value.
// Either output can be empty (no '.' → frac is ""). Mirrors the helper
// in label_widget.cpp.
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

// Tag structure stored in LVGL user data
struct GaugeTag {
    lv_obj_t *valueLabel;
    lv_obj_t *fracLabel; // Fractional-part label rendered at ~70 % of the
                         // integer font. nullptr when decimalPlaces == 0.
    lv_obj_t *unitLabel;
    lv_obj_t *fillArc; // Gradient mode only — value arc whose colour is
                       // recomputed each tick. nullptr in zones mode.
    float minValue;
    float maxValue;
    float lastValue;
    float alertThreshold;    // NaN = disabled (issue #133)
    float revFlashThreshold; // NaN = revFlash disabled or no revLimitRpm (issue #263).
                             // Stored as `revLimitRpm - 1.0f` so AlertFlash's
                             // strict `value > threshold` test triggers at the
                             // first RPM sample that reaches the limit.
    // Single threshold (issue #965). 0 = no threshold configured.
    uint16_t dangerAngle;
    bool hasDanger;
    bool gradientMode;        // True when arcFillStyle == GRADIENT (issue #175)
    bool lastValid;           // Tracks the last (value, valid) pair so the invalid
                              // branch can skip its lv_label_set_text reallocation
                              // when state hasn't flipped (issue #236).
    const CfgColorRamp *ramp; // Active color ramp (issue #430). nullptr → static path.
    // Issue #954: semantic two-zone palette resolved from `gauge.iconName`.
    // Wins over `ramp` and the legacy zone path; nullptr → palette disabled.
    const SensorPaletteEntry *palette;
    float dangerLevel; // Mirrored from `cfg.gauge.dangerLevel` for the palette
                       // threshold test on each update() tick.
    AlertFlash::State alert;
    // Last colours pushed to LVGL — used by the per-frame write guards. Init
    // to 0xFFFFFFFFu so the first update() always paints (alpha bits never
    // appear in a 0x00RRGGBB target so the sentinel is unique).
    uint32_t lastLabelRgb;
    uint32_t lastFillRgb;
};

// GaugeTag storage comes from the shared WidgetTagPool slab (#1031
// F-HI-2 follow-up). See ui/widgets/widget_tag_pool.h.

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

// ---------------------------------------------------------------------------
// create() phase helpers
// ---------------------------------------------------------------------------

// Arc diameter: smallest of w/h, minus padding, floored to kMinArcDiam.
static int32_t computeArcDiameter(const CfgWidget &cfg) {
    int32_t diam =
        (cfg.layout.w < cfg.layout.h ? cfg.layout.w : cfg.layout.h) - kArcContainerPadding;
    if (diam < kMinArcDiam)
        diam = kMinArcDiam;
    return diam;
}

// Lay down the static background tracks (sector arcs). Render order: first
// created = bottom, last created = top. Palette + gradient modes share the
// same single mid-grey base track; zones mode emits two coloured halves
// split at `dangerAngle`; no-threshold mode falls back to a dim full ring.
static void buildBackgroundTracks(lv_obj_t *cont, int32_t diam, bool paletteMode, bool gradientMode,
                                  bool hasDanger, uint16_t dangerAngle) {
    if (paletteMode || gradientMode) {
        // Single mid-grey base track — the value arc tints over it.
        createSectorArc(cont, diam, 0, 280, kColorGradientBg, kBgWidth);
    } else if (!hasDanger) {
        // No threshold — single dimmed background track (old behavior).
        createSectorArc(cont, diam, 0, 280, kColorBgDim, kBgWidth);
    } else {
        // Two zones (issue #965): green (0→danger), red (danger→280).
        createSectorArc(cont, diam, 0, dangerAngle, WidgetHelpers::kZoneNormalRgb, kBgWidth);
        createSectorArc(cont, diam, dangerAngle, 280, WidgetHelpers::kZoneDangerRgb, kBgWidth);
    }
}

// Build the value fill arc that renders on top of the sector/base tracks.
// Palette + gradient modes paint at full kBgWidth so the value arc fully
// covers the base track; zones mode uses the thinner kIndWidth so sector
// zones remain visible around it.
static lv_obj_t *buildValueFillArc(lv_obj_t *cont, int32_t diam,
                                   const SensorPaletteEntry *paletteEntry, bool paletteMode,
                                   bool gradientMode, bool hasDanger) {
    const uint8_t fillW = (gradientMode || paletteMode) ? kBgWidth : kIndWidth;
    lv_obj_t *fillArc = createValueArc(cont, diam, fillW);
    lv_arc_set_angles(fillArc, 0, 0);
    const uint32_t startColor =
        paletteMode ? paletteEntry->okColor
                    : ((hasDanger || gradientMode) ? WidgetHelpers::kZoneNormalRgb : kColorValue);
    lv_obj_set_style_arc_color(fillArc, lv_color_hex(startColor), LV_PART_INDICATOR);
    return fillArc;
}

// Pick the integer-tier font size by container height, mirroring the
// original three-tier ladder (20 / 24 / 32). The fractional label picks
// a smaller font derived from this size.
//
// TODO(#18): the 80 / 110 px thresholds and the 20/24/32 Orbitron sizes are
// hard-coded against the v1 design canvas (320×240). When a second screen
// profile lands the thresholds must be scaled with `ScreenProfile::scaleYVal`
// AND the font sizes themselves need to scale (either via additional baked
// Orbitron tiers or by swapping the font family entirely). v1 keeps the
// scale at 1.0 so no behaviour change today.
static const lv_font_t *resolveValueFont(const CfgWidget &cfg, uint8_t &intFontSizeOut) {
    const lv_font_t *font = FontManager::secondary(20);
    intFontSizeOut = 20;
    if (cfg.layout.h >= 80) {
        font = FontManager::secondary(24);
        intFontSizeOut = 24;
    }
    if (cfg.layout.h >= 110) {
        font = FontManager::primary(32);
        intFontSizeOut = 32;
    }
    return font;
}

// Build the value row — flex container holding [int][frac] baseline-aligned
// so gauges with decimals render the fractional digits at ~70 % of the
// integer font. Matches the numeric widget treatment from PR #975.
static lv_obj_t *buildValueRow(lv_obj_t *cont, bool hasUnit) {
    lv_obj_t *valueRow = lv_obj_create(cont);
    lv_obj_set_size(valueRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(valueRow, LV_ALIGN_CENTER, 0, hasUnit ? -8 : 0);
    lv_obj_set_style_bg_opa(valueRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(valueRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(valueRow, 2, LV_PART_MAIN);
    lv_obj_clear_flag(valueRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    return valueRow;
}

// Create the integer value label and seed it with the formatted zero so the
// first frame paints something readable before update() runs.
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

// Fractional-part label — created only when the gauge actually has decimals.
// For integer-only readouts (speed, RPM, coolant, boost) the slot is skipped
// so we don't burn an LVGL obj. Returns nullptr when not needed.
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

// Optional unit label below the value. Returns nullptr when the resolved
// unit string is empty.
static lv_obj_t *buildUnitLabel(lv_obj_t *cont, const char *unitText, uint32_t textRgb) {
    if (unitText[0] == '\0')
        return nullptr;
    lv_obj_t *unitLabel = lv_label_create(cont);
    lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(textRgb & 0x888888), 0);
    lv_obj_set_style_text_font(unitLabel, FontManager::label(12), 0);
    lv_label_set_text(unitLabel, unitText);
    return unitLabel;
}

// Snapshot the revFlash trigger (issue #263) so update() doesn't have to
// consult ConfigLoader on every tick. The `- 1.0f` offset turns
// AlertFlash's strict `>` test into a `>=` for the integer-stepped RPM
// signal. Returns NaN when revFlash is disabled or the dashboard config
// has no rev-limit RPM.
static float resolveRevFlashThreshold(const CfgWidget &cfg) {
    if (!cfg.gauge.revFlash)
        return NAN;
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || dash.revLimitRpm <= 0.0f)
        return NAN;
    return dash.revLimitRpm - 1.0f;
}

// Resolved arc-mode flags consumed by every downstream helper. Centralising
// these keeps create() free of the threshold/palette/gradient branch noise.
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
    // Single danger threshold (issue #965). A real threshold is configured
    // when dangerLevel sits strictly inside (minValue, maxValue).
    m.hasDanger = !std::isnan(cfg.gauge.dangerLevel) && cfg.gauge.dangerLevel > minV &&
                  cfg.gauge.dangerLevel < maxV;
    m.dangerAngle = m.hasDanger ? valueToAngle(cfg.gauge.dangerLevel, minV, maxV) : 0;
    // Issue #954 — palette mode wins over zone tinting when the gauge pins
    // a known SensorIconName.
    m.paletteEntry = SensorPalette::lookup(cfg.gauge.iconName);
    m.paletteMode = m.paletteEntry != nullptr;
    m.gradientMode = (cfg.gauge.arcFillStyle == CfgArcFillStyle::GRADIENT);
    return m;
}

// Bundle of phase outputs threaded into initGaugeTag() — keeps the helper
// signature readable while still passing every constructed widget pointer.
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

// Build the entire centred value cluster — value row, integer label,
// optional fractional label, optional unit label, and the corner overlay
// label. Populates `outLabel`/`outFrac`/`outUnit` so the caller can hand
// them straight to initGaugeTag().
static void buildValueCluster(lv_obj_t *cont, const CfgWidget &cfg, uint32_t textRgb,
                              lv_obj_t *&outLabel, lv_obj_t *&outFrac, lv_obj_t *&outUnit) {
    const char *unitText = WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.gauge.suffix);
    const bool hasUnit = unitText[0] != '\0';
    uint8_t intFontSize = 20;
    const lv_font_t *font = resolveValueFont(cfg, intFontSize);
    lv_obj_t *valueRow = buildValueRow(cont, hasUnit);
    outLabel = buildValueLabel(valueRow, font, textRgb, cfg);
    outFrac = buildFracLabel(valueRow, cfg, intFontSize, textRgb);
    outUnit = buildUnitLabel(cont, unitText, textRgb);
    WidgetLabelOverlay::apply(cont, cfg.gauge.label, cfg.gauge.labelPosition, textRgb);
}

// Populate the Tag struct from the widget config + the just-built widget
// pointers. The color-ramp resolution sits here (issue #430) — palette
// mode wins, otherwise we consult the per-signal ramp; null means the
// legacy threshold-driven static colors are used unchanged.
static void initGaugeTag(GaugeTag *tag, const CfgWidget &cfg, const GaugeBuildState &built) {
    tag->valueLabel = built.label;
    tag->fracLabel = built.fracLabel;
    tag->unitLabel = built.unitLabel;
    tag->fillArc = built.fillArc;
    tag->minValue = cfg.gauge.minValue;
    tag->maxValue = cfg.gauge.maxValue;
    // NaN sentinel — guarantees the first update() runs through the paint
    // path even when the live value happens to be 0.0 (matches bar_widget).
    tag->lastValue = NAN;
    tag->alertThreshold = cfg.gauge.alertThreshold;
    tag->dangerAngle = built.dangerAngle;
    tag->hasDanger = built.hasDanger;
    tag->gradientMode = built.gradientMode;
    tag->lastValid = true;
    tag->lastLabelRgb = 0xFFFFFFFFu;
    tag->lastFillRgb = 0xFFFFFFFFu;
    tag->palette = built.paletteEntry;
    tag->dangerLevel = cfg.gauge.dangerLevel;
    tag->ramp = built.paletteEntry ? nullptr : WidgetHelpers::resolveSignalRamp(cfg.signalId);
    tag->revFlashThreshold = resolveRevFlashThreshold(cfg);
}

// Mount the alert overlay last so it sits on top of arcs and labels, and
// register each text label so AlertFlash can repaint them in lockstep.
static void attachAlertFlash(GaugeTag *tag, lv_obj_t *cont, uint32_t textRgb) {
    AlertFlash::attach(tag->alert, cont);
    AlertFlash::watchLabel(tag->alert, tag->valueLabel, textRgb);
    if (tag->fracLabel) {
        AlertFlash::watchLabel(tag->alert, tag->fracLabel, textRgb);
    }
    if (tag->unitLabel) {
        AlertFlash::watchLabel(tag->alert, tag->unitLabel, textRgb & 0x888888);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GaugeWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const int32_t diam = computeArcDiameter(cfg);
    const GaugeArcModes modes = resolveArcModes(cfg);
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    buildBackgroundTracks(cont, diam, modes.paletteMode, modes.gradientMode, modes.hasDanger,
                          modes.dangerAngle);
    lv_obj_t *fillArc = buildValueFillArc(cont, diam, modes.paletteEntry, modes.paletteMode,
                                          modes.gradientMode, modes.hasDanger);

    lv_obj_t *label = nullptr;
    lv_obj_t *fracLabel = nullptr;
    lv_obj_t *unitLabel = nullptr;
    buildValueCluster(cont, cfg, textRgb, label, fracLabel, unitLabel);

    GaugeTag *tag = WidgetTagPool::alloc<GaugeTag>();
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
    attachAlertFlash(tag, cont, textRgb);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GaugeTag>, LV_EVENT_DELETE, tag);

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
            WidgetStyles::setTextColorIfChanged(
                tag->valueLabel, tag->lastLabelRgb,
                ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb,
                                                    cfg.style.respectDayMode));
        }
        // Collapse the fill arc to zero on invalid signals so the base track
        // is fully visible — matches the "show 0" rule above.
        if (tag->fillArc) {
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

    // Tint the value label — skip when AlertFlash owns the colour. Palette
    // mode (#954) drives both fill AND label colour so the read matches. The
    // legacy ramp path (issue #430) still applies when no palette is pinned.
    if (!tag->alert.active) {
        uint32_t labelColor =
            ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
        if (tag->palette) {
            labelColor = SensorPalette::fillColor(cfg.gauge.iconName, value, tag->dangerLevel);
        } else if (tag->ramp) {
            labelColor = colorAtValue(*tag->ramp, value);
        } else if (tag->hasDanger && value >= cfg.gauge.dangerLevel) {
            labelColor = WidgetHelpers::kZoneDangerRgb;
        }
        WidgetStyles::setTextColorIfChanged(tag->valueLabel, tag->lastLabelRgb, labelColor);
        if (tag->fracLabel) {
            uint32_t fracLast = tag->lastLabelRgb;
            WidgetStyles::setTextColorIfChanged(tag->fracLabel, fracLast, labelColor);
        }
    }

    // Update fill arc angle and colour. Order: palette > ramp > gradient >
    // two-zone (danger) > white fallback (issue #965).
    if (tag->fillArc) {
        const uint16_t angle = valueToAngle(value, tag->minValue, tag->maxValue);
        lv_arc_set_angles(tag->fillArc, 0, angle);

        uint32_t fillColor;
        if (tag->palette) {
            fillColor = SensorPalette::fillColor(cfg.gauge.iconName, value, tag->dangerLevel);
        } else if (tag->ramp) {
            fillColor = colorAtValue(*tag->ramp, value);
        } else if (tag->gradientMode) {
            const float range = tag->maxValue - tag->minValue;
            const float pct = range > 0.0f ? (value - tag->minValue) / range : 0.0f;
            fillColor = interpolateGreenOrangeRed(pct);
        } else if (tag->hasDanger) {
            fillColor = value >= cfg.gauge.dangerLevel ? WidgetHelpers::kZoneDangerRgb
                                                       : WidgetHelpers::kZoneNormalRgb;
        } else {
            fillColor = kColorValue; // white — no threshold, no ramp
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
