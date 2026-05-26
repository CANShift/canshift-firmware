// bar_widget.cpp — Horizontal / vertical bar widget that mirrors studio's
// GaugeBarPreview: a proportional track with translucent warning/danger
// zones, a threshold-coloured fill, signal label, value readout, and an
// optional widget label at a chosen corner. `create()` split into 19 phase
// helpers in #1125 (umbrella #1014); see the file's anonymous-namespace
// block below for the helpers + the orchestrator at the bottom.

#include "bar_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/sensor_color_ramp.h"
#include "ui/sensor_palette.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// Layout constants (mirrored from studio's pixel ratios).
constexpr float TRACK_H_RATIO = 0.35f; // horizontal: track height vs widget height
constexpr float TRACK_W_RATIO = 0.60f; // vertical: track width vs widget width
constexpr int16_t HORIZ_PAD_X = 6;
constexpr int16_t MIN_TRACK_DIM = 4;

struct BarTag {
    lv_obj_t *track;
    lv_obj_t *dangerZone; // nullable — only when dangerLevel < maxValue
    lv_obj_t *dangerTick; // nullable
    lv_obj_t *fill;
    lv_obj_t *signalLabel;
    lv_obj_t *valueLabel;  // nullable on very small widgets
    lv_obj_t *suffixLabel; // nullable — shown below value for vertical layout
    lv_obj_t *widgetLabel; // nullable
    lv_obj_t *iconImg;     // nullable
    bool isVertical;
    int16_t trackX, trackY, trackW, trackH;
    float minValue, maxValue;
    float dangerLevel;    // Single threshold (issue #965). NaN = no threshold.
    float alertThreshold; // NaN = disabled (issue #133)
    uint32_t primaryRgb, warningRgb, criticalRgb;
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[8];
    float lastValue;
    bool wasValid;
    const CfgColorRamp *ramp; // Active color ramp (issue #430). nullptr → legacy zone tints.
    const SensorPaletteEntry *palette; // Two-zone palette (issue #954). Wins over ramp+zones.
    char iconName[16];                 // Snapshot for the palette lookup on each tick.
    AlertFlash::State alert;
    // Cached fill colour pushed to LVGL — guards skip the redundant style
    // write when the zone-driven colour hasn't changed since last frame.
    // Init to 0xFFFFFFFFu so the first update() always paints.
    uint32_t lastFillRgb;
};

// BarTag storage comes from the shared WidgetTagPool slab (#1031 F-HI-2
// follow-up). See ui/widgets/widget_tag_pool.h.

// Studio uses a faint grey for label text when dimmed. We use the same fixed
// tone so the bar reads identically across themes.
constexpr uint32_t SIGNAL_LABEL_RGB = 0x888888;
constexpr uint32_t TRACK_BG_RGB = 0x1C1C1C;
constexpr uint32_t TICK_LABEL_RGB = 0x383838;
constexpr uint32_t VALUE_TEXT_RGB = 0xFFFFFF; // value % always white per user spec
constexpr lv_opa_t ZONE_OPA = 0x35;

// Horizontal label band height tuning. The 14-px floor matches Orbitron
// Medium 12's line height — anything tighter visibly clips the value
// ("65%") and the signal name. The 24-px cap keeps the bar dominant on
// tall widgets. The 25 % ratio mirrors studio's GaugeBarPreview.
constexpr int16_t HORIZ_BAND_RATIO_PCT = 25;
constexpr int16_t HORIZ_BAND_MIN_H = 14;
constexpr int16_t HORIZ_BAND_MAX_H = 24;
constexpr int16_t HORIZ_BAND_GAP = 2;
constexpr int16_t HORIZ_VAL_MIN_H = 14;       // Skip the value label below this track height
constexpr int16_t HORIZ_VAL_LARGE_TRACK = 24; // Use the bigger value font above this
constexpr int16_t HORIZ_VAL_FONT_SM = 12;
constexpr int16_t HORIZ_VAL_FONT_LG = 14;

// Vertical layout font tuning — same H >= 80 break as the GaugeBarPreview.
constexpr int16_t VERT_LARGE_BREAK_H = 80;
constexpr int16_t VERT_MIN_BAR_W = 10;

void renderValueText(BarTag *t, float v) {
    if (!t->valueLabel)
        return;
    char buf[24];
    const char *suffix = t->isVertical ? "" : t->suffix;
    WidgetHelpers::formatValue(buf, sizeof(buf), t->prefix, t->decimalPlaces, v, suffix);
    lv_label_set_text(t->valueLabel, buf);
}

// ---------------------------------------------------------------------------
// create() phase helpers
// ---------------------------------------------------------------------------

// Seed every Tag field from the widget config. Resolves the per-widget
// suffix (signals.json default, per-widget override wins — same resolution
// as the numeric label widget), the two-zone palette (#954, pins a known
// SensorIconName) and the color ramp (#430). The `lastValue = NAN` is a
// sentinel that guarantees the first update() runs through the paint path
// even if minValue happens to be 0.
void initBarTag(BarTag *t, const CfgWidget &cfg) {
    t->isVertical = cfg.bar.isVertical;
    t->minValue = cfg.bar.minValue;
    t->maxValue = cfg.bar.maxValue;
    t->dangerLevel = cfg.bar.dangerLevel;
    t->alertThreshold = cfg.bar.alertThreshold;
    t->primaryRgb = cfg.style.primaryColor.rgb;
    t->warningRgb = cfg.style.warningColor.rgb;
    t->criticalRgb = cfg.style.criticalColor.rgb;
    t->decimalPlaces = cfg.bar.decimalPlaces;
    strlcpy(t->prefix, cfg.bar.prefix, sizeof(t->prefix));
    strlcpy(t->suffix, WidgetHelpers::resolveDisplayUnit(cfg.signalId, cfg.bar.suffix),
            sizeof(t->suffix));
    t->lastValue = NAN;
    t->wasValid = false;
    t->lastFillRgb = 0xFFFFFFFFu;
    strlcpy(t->iconName, cfg.bar.iconName, sizeof(t->iconName));
    t->palette = SensorPalette::lookup(t->iconName);
    t->ramp = t->palette ? nullptr : WidgetHelpers::resolveSignalRamp(cfg.signalId);
}

// Allocate the Tag from the shared pool. Returns nullptr (and logs) when
// the pool is exhausted — the caller must destroy the half-built container
// in that case.
BarTag *allocBarTag(const CfgWidget &cfg) {
    BarTag *t = WidgetTagPool::alloc<BarTag>();
    if (!t) {
        LOG_WARN("BAR", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        return nullptr;
    }
    initBarTag(t, cfg);
    return t;
}

// Resolved danger-band geometry shared by both layout branches. Palette
// mode (#954) replaces the translucent band with a solid per-sensor fill,
// so suppress the band geometry when a palette is pinned.
struct DangerBand {
    bool hasDanger;
    float dangerPct;
};

DangerBand resolveDangerBand(const BarTag *t) {
    DangerBand b{};
    b.hasDanger = !std::isnan(t->dangerLevel) && t->palette == nullptr;
    b.dangerPct =
        b.hasDanger ? WidgetHelpers::clampPct(t->dangerLevel, t->minValue, t->maxValue) : 1.0f;
    return b;
}

// Computed horizontal layout — label band on one side of the widget, track
// on the other. The band side follows the user's `cfg.bar.labelPosition`
// (or defaults to top when no user label) so a corner-anchored user label
// can never sit ON TOP of the bar fill.
struct HorizLayout {
    int16_t labelBandH;
    int16_t barH;
    int16_t trackY;
    int16_t bandY;
    int16_t trackW;
    bool noUserLabel;
    bool labelIsTop;
};

HorizLayout computeHorizLayout(const CfgWidget &cfg, int16_t W, int16_t H) {
    HorizLayout h{};
    h.noUserLabel = (cfg.bar.label[0] == '\0');
    h.labelIsTop = h.noUserLabel || cfg.bar.labelPosition == CfgLabelPos::TOP_LEFT ||
                   cfg.bar.labelPosition == CfgLabelPos::TOP_CENTER ||
                   cfg.bar.labelPosition == CfgLabelPos::TOP_RIGHT;
    h.labelBandH = static_cast<int16_t>((H * HORIZ_BAND_RATIO_PCT) / 100);
    if (h.labelBandH < HORIZ_BAND_MIN_H)
        h.labelBandH = HORIZ_BAND_MIN_H;
    if (h.labelBandH > HORIZ_BAND_MAX_H)
        h.labelBandH = HORIZ_BAND_MAX_H;
    h.barH = H - h.labelBandH - HORIZ_BAND_GAP;
    h.trackY = h.labelIsTop ? h.labelBandH + HORIZ_BAND_GAP : 0;
    h.bandY = h.labelIsTop ? 0 : h.barH + HORIZ_BAND_GAP;
    h.trackW = W - HORIZ_PAD_X * 2;
    return h;
}

// Track background — square corners per user spec. Caches the geometry
// onto the Tag so update() can resize the fill without reading the LVGL
// object back.
void buildHorizTrack(lv_obj_t *cont, BarTag *t, const HorizLayout &lay) {
    t->trackX = HORIZ_PAD_X;
    t->trackY = lay.trackY;
    t->trackW = lay.trackW;
    t->trackH = lay.barH;
    lv_obj_t *track = lv_obj_create(cont);
    lv_obj_set_pos(track, HORIZ_PAD_X, lay.trackY);
    lv_obj_set_size(track, lay.trackW, lay.barH);
    WidgetStyles::applyBarTrack(track);
    lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
    t->track = track;
}

// Danger zone (danger..max band) — single zone (issue #965). No-op when
// the band collapses to zero width.
void buildHorizDangerZone(lv_obj_t *cont, BarTag *t, const HorizLayout &lay,
                          const DangerBand &band) {
    if (!band.hasDanger || band.dangerPct >= 1.0f)
        return;
    int16_t zX = HORIZ_PAD_X + static_cast<int16_t>(lay.trackW * band.dangerPct);
    int16_t zW = static_cast<int16_t>(lay.trackW * (1.0f - band.dangerPct));
    lv_obj_t *zone = lv_obj_create(cont);
    WidgetHelpers::disableInteract(zone);
    lv_obj_set_pos(zone, zX, lay.trackY);
    lv_obj_set_size(zone, zW, lay.barH);
    WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneDangerRgb, ZONE_OPA);
    t->dangerZone = zone;
}

// Fill obj — width is updated dynamically. Starts at 0. Colour set in
// update() based on the active zone (green/orange/red).
void buildHorizFill(lv_obj_t *cont, BarTag *t, const HorizLayout &lay) {
    lv_obj_t *fill = lv_obj_create(cont);
    WidgetHelpers::disableInteract(fill);
    lv_obj_set_pos(fill, HORIZ_PAD_X, lay.trackY);
    lv_obj_set_size(fill, 0, lay.barH);
    WidgetStyles::applyBarFill(fill, WidgetHelpers::kZoneNormalRgb);
    t->fill = fill;
}

// Signal label — only when the user hasn't supplied a custom one. Lives
// in the label band, NOT over the track.
void buildHorizSignalLabel(lv_obj_t *cont, BarTag *t, const HorizLayout &lay, const char *sigBuf) {
    if (!lay.noUserLabel)
        return;
    lv_obj_t *sig = lv_label_create(cont);
    lv_label_set_text(sig, sigBuf);
    lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
    lv_obj_set_style_text_font(sig, FontManager::label(HORIZ_VAL_FONT_SM), 0);
    lv_obj_set_style_text_letter_space(sig, 1, 0);
    lv_obj_set_pos(sig, 2, lay.bandY + 1);
    t->signalLabel = sig;
}

// Value label — white, centred ON the bar track (not the label band). Sits
// over the fill so it reads as part of the bar. Skipped when the track is
// too thin to fit any readable font.
void buildHorizValueLabel(lv_obj_t *cont, BarTag *t, int16_t W, const HorizLayout &lay) {
    if (lay.barH < HORIZ_VAL_MIN_H)
        return;
    const lv_font_t *valFont = FontManager::label(
        lay.barH >= HORIZ_VAL_LARGE_TRACK ? HORIZ_VAL_FONT_LG : HORIZ_VAL_FONT_SM);
    lv_obj_t *val = lv_label_create(cont);
    lv_obj_set_style_text_color(val, lv_color_hex(VALUE_TEXT_RGB), 0);
    lv_obj_set_style_text_font(val, valFont, 0);
    lv_obj_set_width(val, W);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    const int16_t fontH = lv_font_get_line_height(valFont);
    const int16_t valueY = lay.trackY + (lay.barH - fontH) / 2;
    lv_obj_set_pos(val, 0, valueY);
    t->valueLabel = val;
}

// Map the user's full vertical+horizontal label position to the band's
// inner position — the band itself is already on the right side of the
// widget, so we only need horizontal alignment + a top anchor.
CfgLabelPos mapBandInnerPos(CfgLabelPos outer) {
    switch (outer) {
        case CfgLabelPos::TOP_LEFT:
        case CfgLabelPos::BOTTOM_LEFT:
            return CfgLabelPos::TOP_LEFT;
        case CfgLabelPos::TOP_CENTER:
        case CfgLabelPos::BOTTOM_CENTER:
            return CfgLabelPos::TOP_CENTER;
        case CfgLabelPos::TOP_RIGHT:
        case CfgLabelPos::BOTTOM_RIGHT:
            return CfgLabelPos::TOP_RIGHT;
    }
    return CfgLabelPos::TOP_LEFT;
}

// Optional widget label — sits in the label band at the user-chosen
// corner. WidgetLabelOverlay aligns to the container, so we anchor through
// a transient inner stripe matched to the band.
void buildHorizUserLabel(lv_obj_t *cont, const CfgWidget &cfg, const HorizLayout &lay, int16_t W) {
    if (lay.noUserLabel)
        return;
    lv_obj_t *band = lv_obj_create(cont);
    WidgetHelpers::disableInteract(band);
    lv_obj_set_pos(band, 0, lay.bandY);
    lv_obj_set_size(band, W, lay.labelBandH);
    lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(band, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(band, 0, LV_PART_MAIN);
    const CfgLabelPos innerPos = mapBandInnerPos(cfg.bar.labelPosition);
    WidgetLabelOverlay::apply(
        band, cfg.bar.label, innerPos,
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode));
}

// Optional icon (drawn at left edge of the band, before any label).
void buildHorizIcon(lv_obj_t *cont, BarTag *t, const CfgWidget &cfg, const HorizLayout &lay) {
    if (cfg.bar.iconName[0] == '\0')
        return;
    const char *path = IconAssets::path(cfg.bar.iconName);
    if (path[0] == '\0')
        return;
    lv_obj_t *img = lv_img_create(cont);
    lv_img_set_src(img, path);
    lv_obj_set_style_img_recolor(img, lv_color_hex(SIGNAL_LABEL_RGB), 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, 1, lay.bandY + 1);
    t->iconImg = img;
}

// Horizontal layout orchestrator — band on one side, track on the other.
void buildHorizontal(lv_obj_t *cont, const CfgWidget &cfg, BarTag *t, const DangerBand &band,
                     const char *sigBuf) {
    const int16_t W = cfg.layout.w;
    const int16_t H = cfg.layout.h;
    const HorizLayout lay = computeHorizLayout(cfg, W, H);
    buildHorizTrack(cont, t, lay);
    buildHorizDangerZone(cont, t, lay, band);
    buildHorizFill(cont, t, lay);
    buildHorizSignalLabel(cont, t, lay, sigBuf);
    buildHorizValueLabel(cont, t, W, lay);
    buildHorizUserLabel(cont, cfg, lay, W);
    buildHorizIcon(cont, t, cfg, lay);
}

// Computed vertical layout — fixed-width track centred horizontally, with
// reserved bands top (signal) and bottom (value + suffix). Matches studio's
// GaugeBarPreview vertical branch.
struct VertLayout {
    int16_t padX;
    int16_t padTop;
    int16_t barW;
    int16_t trackH;
    int16_t sigLabelH;
    int16_t valLabelH;
    int16_t suffixH;
};

VertLayout computeVertLayout(int16_t W, int16_t H) {
    VertLayout v{};
    v.barW = static_cast<int16_t>(fmaxf(static_cast<float>(VERT_MIN_BAR_W), W * TRACK_W_RATIO));
    v.padX = (W - v.barW) / 2;
    v.sigLabelH = H >= VERT_LARGE_BREAK_H ? 14 : 12;
    v.valLabelH = H >= VERT_LARGE_BREAK_H ? 16 : 14;
    v.suffixH = H >= VERT_LARGE_BREAK_H ? 12 : 10;
    v.padTop = v.sigLabelH + 3;
    const int16_t padBot = v.valLabelH + v.suffixH + 6;
    v.trackH = static_cast<int16_t>(fmaxf(MIN_TRACK_DIM, H - v.padTop - padBot));
    return v;
}

// Vertical track background. Caches geometry onto the Tag so update()
// can resize the fill without reading the LVGL object back.
void buildVertTrack(lv_obj_t *cont, BarTag *t, const VertLayout &lay) {
    t->trackX = lay.padX;
    t->trackY = lay.padTop;
    t->trackW = lay.barW;
    t->trackH = lay.trackH;
    lv_obj_t *track = lv_obj_create(cont);
    lv_obj_set_pos(track, lay.padX, lay.padTop);
    lv_obj_set_size(track, lay.barW, lay.trackH);
    WidgetStyles::applyBarTrack(track);
    lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
    t->track = track;
}

// Vertical danger zone — band from the top of the track down to
// dangerLevel (single zone, issue #965). No-op when the band collapses.
void buildVertDangerZone(lv_obj_t *cont, BarTag *t, const VertLayout &lay, const DangerBand &band) {
    if (!band.hasDanger || band.dangerPct >= 1.0f)
        return;
    int16_t topZH = static_cast<int16_t>(lay.trackH * (1.0f - band.dangerPct));
    if (topZH <= 0)
        return;
    lv_obj_t *zone = lv_obj_create(cont);
    WidgetHelpers::disableInteract(zone);
    lv_obj_set_pos(zone, lay.padX, lay.padTop);
    lv_obj_set_size(zone, lay.barW, topZH);
    WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneDangerRgb, ZONE_OPA);
    t->dangerZone = zone;
}

// Vertical fill — sits at the bottom of the track, height grows upwards.
void buildVertFill(lv_obj_t *cont, BarTag *t, const VertLayout &lay) {
    lv_obj_t *fill = lv_obj_create(cont);
    WidgetHelpers::disableInteract(fill);
    lv_obj_set_pos(fill, lay.padX, lay.padTop + lay.trackH);
    lv_obj_set_size(fill, lay.barW, 0);
    WidgetStyles::applyBarFill(fill, t->primaryRgb);
    t->fill = fill;
}

// Vertical labels — signal (top centre, dropped when user set a custom
// label), value (bottom centre, large), and optional suffix below.
void buildVertLabels(lv_obj_t *cont, BarTag *t, const CfgWidget &cfg, const VertLayout &lay,
                     const char *sigBuf) {
    if (cfg.bar.label[0] == '\0') {
        lv_obj_t *sig = lv_label_create(cont);
        lv_label_set_text(sig, sigBuf);
        lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
        lv_obj_set_style_text_font(sig, FontManager::label(lay.sigLabelH), 0);
        lv_obj_align(sig, LV_ALIGN_TOP_MID, 0, 1);
        t->signalLabel = sig;
    }
    lv_obj_t *val = lv_label_create(cont);
    lv_obj_set_style_text_color(val, lv_color_hex(t->primaryRgb), 0);
    lv_obj_set_style_text_font(val, FontManager::label(lay.valLabelH), 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -lay.suffixH - 1);
    t->valueLabel = val;
    if (t->suffix[0] != '\0') {
        lv_obj_t *suffix = lv_label_create(cont);
        lv_label_set_text(suffix, t->suffix);
        lv_obj_set_style_text_color(suffix, lv_color_hex(SIGNAL_LABEL_RGB), 0);
        lv_obj_set_style_text_font(suffix, FontManager::label(lay.suffixH), 0);
        lv_obj_align(suffix, LV_ALIGN_BOTTOM_MID, 0, -1);
        t->suffixLabel = suffix;
    }
}

// Vertical layout orchestrator — matches GaugeBarPreview vertical branch.
void buildVertical(lv_obj_t *cont, const CfgWidget &cfg, BarTag *t, const DangerBand &band,
                   const char *sigBuf) {
    const VertLayout lay = computeVertLayout(cfg.layout.w, cfg.layout.h);
    buildVertTrack(cont, t, lay);
    buildVertDangerZone(cont, t, lay, band);
    buildVertFill(cont, t, lay);
    buildVertLabels(cont, t, cfg, lay, sigBuf);
}

// Mount the alert overlay last so it sits on top of all bar elements.
// Track the value label so it flips to white during a flash (signalLabel
// and suffixLabel already use a fixed dim grey — leave them alone for
// contrast).
void attachBarAlertFlash(lv_obj_t *cont, BarTag *t) {
    AlertFlash::attach(t->alert, cont);
    if (t->valueLabel)
        AlertFlash::watchLabel(t->alert, t->valueLabel, VALUE_TEXT_RGB);
}

} // namespace

lv_obj_t *BarWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    BarTag *t = allocBarTag(cfg);
    if (!t) {
        lv_obj_del(cont);
        return nullptr;
    }

    const DangerBand band = resolveDangerBand(t);
    char sigBuf[CFG_MAX_SIGNAL_LEN + 4];
    WidgetHelpers::formatSignalLabel(cfg.signalId, sigBuf, sizeof(sigBuf));

    if (!t->isVertical) {
        buildHorizontal(cont, cfg, t, band, sigBuf);
    } else {
        buildVertical(cont, cfg, t, band, sigBuf);
    }

    attachBarAlertFlash(cont, t);
    lv_obj_set_user_data(cont, t);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<BarTag>, LV_EVENT_DELETE, t);

    // Initial paint: invalid → 0 (per design).
    BarWidget::update(cont, cfg.bar.minValue, false, cfg);

    LOG_DEBUG("BAR", "Created %s bar '%s' at (%d,%d) size=%dx%d range=[%d,%d]",
              t->isVertical ? "vertical" : "horizontal", cfg.id, cfg.layout.x,
              cfg.layout.y + yOffset, cfg.layout.w, cfg.layout.h,
              static_cast<int>(lroundf(cfg.bar.minValue)),
              static_cast<int>(lroundf(cfg.bar.maxValue)));
    return cont;
}

void BarWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *t = static_cast<BarTag *>(lv_obj_get_user_data(obj));
    if (!t || !t->fill || !t->valueLabel)
        return;

    // When the signal is missing, studio shows the "—" placeholder. The user
    // wants 0 instead so the dash always reads numerically.
    const float displayValue = valid ? value : 0.0f;

    // Skip redundant updates (NaN sentinel forces the first paint through).
    if (valid == t->wasValid && !std::isnan(t->lastValue) && displayValue == t->lastValue)
        return;
    t->lastValue = displayValue;
    t->wasValid = valid;

    const float pct = WidgetHelpers::clampPct(displayValue, t->minValue, t->maxValue);

    // Fill colour priority: palette (#954) → ramp (#430) → two-zone danger
    // (issue #965). Without a configured threshold the fill stays in the
    // OK colour across the entire range.
    uint32_t fillRgb;
    if (t->palette) {
        fillRgb = SensorPalette::fillColor(t->iconName, displayValue, t->dangerLevel);
    } else if (t->ramp) {
        fillRgb = colorAtValue(*t->ramp, displayValue);
    } else if (!std::isnan(t->dangerLevel) && displayValue >= t->dangerLevel) {
        fillRgb = WidgetHelpers::kZoneDangerRgb;
    } else {
        fillRgb = WidgetHelpers::kZoneNormalRgb;
    }
    WidgetStyles::setBgColorIfChanged(t->fill, t->lastFillRgb, fillRgb);

    if (!t->isVertical) {
        int16_t fillW = static_cast<int16_t>(t->trackW * pct);
        lv_obj_set_size(t->fill, fillW, t->trackH);
    } else {
        int16_t fillH = static_cast<int16_t>(t->trackH * pct);
        lv_obj_set_pos(t->fill, t->trackX, t->trackY + t->trackH - fillH);
        lv_obj_set_size(t->fill, t->trackW, fillH);
    }

    if (t->valueLabel) {
        // Keep the value WHITE on the bar — re-tinting it to fillRgb would
        // make it invisible (same colour as the fill underneath).
        renderValueText(t, displayValue);
    }

    // Drive the threshold flash from the live value (NaN threshold = disabled).
    AlertFlash::update(t->alert, displayValue, t->alertThreshold);

    (void)cfg;
}
