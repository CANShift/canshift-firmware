// bar_widget.cpp — Horizontal / vertical bar widget that mirrors studio's
// GaugeBarPreview: a proportional track with translucent warning/danger
// zones, a threshold-coloured fill, signal label, value readout, and an
// optional widget label at a chosen corner.

#include "bar_widget.h"
#include "ui/alert_flash.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/sensor_color_ramp.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
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
    lv_obj_t *warnZone;   // nullable — only when warning..danger range exists
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
    float warningLevel, dangerLevel;
    float alertThreshold; // NaN = disabled (issue #133)
    uint32_t primaryRgb, warningRgb, criticalRgb;
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[8];
    float lastValue;
    bool wasValid;
    const CfgColorRamp *ramp; // Active color ramp (issue #430). nullptr → legacy zone tints.
    AlertFlash::State alert;
    // Cached fill colour pushed to LVGL — guards skip the redundant style
    // write when the zone-driven colour hasn't changed since last frame.
    // Init to 0xFFFFFFFFu so the first update() always paints.
    uint32_t lastFillRgb;
};

// Studio uses a faint grey for label text when dimmed. We use the same fixed
// tone so the bar reads identically across themes.
constexpr uint32_t SIGNAL_LABEL_RGB = 0x888888;
constexpr uint32_t TRACK_BG_RGB = 0x1C1C1C;
constexpr uint32_t TICK_LABEL_RGB = 0x383838;
constexpr uint32_t VALUE_TEXT_RGB = 0xFFFFFF; // value % always white per user spec
constexpr lv_opa_t ZONE_OPA = 0x35;

void renderValueText(BarTag *t, float v) {
    if (!t->valueLabel)
        return;
    char buf[24];
    const char *suffix = t->isVertical ? "" : t->suffix;
    WidgetHelpers::formatValue(buf, sizeof(buf), t->prefix, t->decimalPlaces, v, suffix);
    lv_label_set_text(t->valueLabel, buf);
}

} // namespace

lv_obj_t *BarWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    const bool isVertical = cfg.bar.isVertical;
    const int16_t W = cfg.layout.w;
    const int16_t H = cfg.layout.h;

    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    auto *t = new BarTag{};
    t->isVertical = isVertical;
    t->minValue = cfg.bar.minValue;
    t->maxValue = cfg.bar.maxValue;
    t->warningLevel = cfg.bar.warningLevel;
    t->dangerLevel = cfg.bar.dangerLevel;
    t->alertThreshold = cfg.bar.alertThreshold;
    t->primaryRgb = cfg.style.primaryColor.rgb;
    t->warningRgb = cfg.style.warningColor.rgb;
    t->criticalRgb = cfg.style.criticalColor.rgb;
    t->decimalPlaces = cfg.bar.decimalPlaces;
    strlcpy(t->prefix, cfg.bar.prefix, sizeof(t->prefix));
    strlcpy(t->suffix, cfg.bar.suffix, sizeof(t->suffix));
    // Sentinel that no real value has been pushed yet — guarantees the
    // first update() runs through the paint path even if minValue == 0.
    t->lastValue = NAN;
    t->wasValid = false;
    t->lastFillRgb = 0xFFFFFFFFu;

    // Resolve the active color ramp once (issue #430). Per-signal ramp wins,
    // sensor-name heuristic next, nullptr → legacy zone-based tinting.
    t->ramp = WidgetHelpers::resolveSignalRamp(cfg.signalId);

    const WidgetHelpers::ThresholdZones zones =
        WidgetHelpers::resolveZones(t->warningLevel, t->dangerLevel, t->minValue, t->maxValue);
    const bool hasWarn = zones.hasWarn;
    const bool hasDanger = zones.hasDanger;
    const float warnPct = zones.warnPct;
    const float dangerPct = zones.dangerPct;

    char sigBuf[CFG_MAX_SIGNAL_LEN + 4];
    WidgetHelpers::formatSignalLabel(cfg.signalId, sigBuf, sizeof(sigBuf));

    // -----------------------------------------------------------------------
    // Horizontal layout — label band on one side of the widget, track on the
    // other. The band side follows the user's `cfg.bar.labelPosition` (or
    // defaults to top when no user label, mirroring studio's signal header).
    // Layout chosen so a corner-anchored user label can never sit ON TOP of
    // the bar fill — required because widgets can be placed/sized freely from
    // studio.
    // -----------------------------------------------------------------------
    if (!isVertical) {
        const bool noUserLabel = (cfg.bar.label[0] == '\0');
        const bool labelIsTop = noUserLabel || cfg.bar.labelPosition == CfgLabelPos::TOP_LEFT ||
                                cfg.bar.labelPosition == CfgLabelPos::TOP_CENTER ||
                                cfg.bar.labelPosition == CfgLabelPos::TOP_RIGHT;

        // Reserve a label band: 25 % of widget height, clamped 14..24. The
        // 14-px floor matches Orbitron Medium 12's line height — anything tighter
        // visibly clips the value (\"65%\") and the signal name. The 24-px
        // cap keeps the bar dominant on tall widgets.
        int16_t labelBandH = static_cast<int16_t>((H * 25) / 100);
        if (labelBandH < 14)
            labelBandH = 14;
        if (labelBandH > 24)
            labelBandH = 24;
        // Bar fills the rest minus a small gap.
        const int16_t gap = 2;
        const int16_t barH = H - labelBandH - gap;
        const int16_t trackY = labelIsTop ? labelBandH + gap : 0;
        const int16_t bandY = labelIsTop ? 0 : barH + gap;
        const int16_t trackW = W - HORIZ_PAD_X * 2;
        t->trackX = HORIZ_PAD_X;
        t->trackY = trackY;
        t->trackW = trackW;
        t->trackH = barH;

        // Track background — square corners per user spec
        lv_obj_t *track = lv_obj_create(cont);
        lv_obj_set_pos(track, HORIZ_PAD_X, trackY);
        lv_obj_set_size(track, trackW, barH);
        WidgetStyles::applyBarTrack(track);
        lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
        t->track = track;

        // Warning zone (warning..danger band)
        if (hasWarn && hasDanger && dangerPct > warnPct) {
            int16_t zX = HORIZ_PAD_X + static_cast<int16_t>(trackW * warnPct);
            int16_t zW = static_cast<int16_t>(trackW * (dangerPct - warnPct));
            lv_obj_t *zone = lv_obj_create(cont);
            WidgetHelpers::disableInteract(zone);
            lv_obj_set_pos(zone, zX, trackY);
            lv_obj_set_size(zone, zW, barH);
            WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneWarningRgb, ZONE_OPA);
            t->warnZone = zone;
        }

        // Danger zone (danger..max band)
        if (hasDanger && dangerPct < 1.0f) {
            int16_t zX = HORIZ_PAD_X + static_cast<int16_t>(trackW * dangerPct);
            int16_t zW = static_cast<int16_t>(trackW * (1.0f - dangerPct));
            lv_obj_t *zone = lv_obj_create(cont);
            WidgetHelpers::disableInteract(zone);
            lv_obj_set_pos(zone, zX, trackY);
            lv_obj_set_size(zone, zW, barH);
            WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneDangerRgb, ZONE_OPA);
            t->dangerZone = zone;
        }

        // Fill — width is updated dynamically. Starts at 0. Colour set in
        // update() based on the active zone (green/orange/red).
        lv_obj_t *fill = lv_obj_create(cont);
        WidgetHelpers::disableInteract(fill);
        lv_obj_set_pos(fill, HORIZ_PAD_X, trackY);
        lv_obj_set_size(fill, 0, barH);
        WidgetStyles::applyBarFill(fill, WidgetHelpers::kZoneNormalRgb);
        t->fill = fill;

        // Signal label — only when the user hasn't supplied a custom one.
        // Lives in the label band, NOT over the track.
        if (noUserLabel) {
            lv_obj_t *sig = lv_label_create(cont);
            lv_label_set_text(sig, sigBuf);
            lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(sig, FontManager::label(12), 0);
            lv_obj_set_style_text_letter_space(sig, 1, 0);
            lv_obj_set_pos(sig, 2, bandY + 1);
            t->signalLabel = sig;
        }

        // Value label — white, centred ON the bar track (not the label band).
        // Sits over the fill so it reads as part of the bar.
        if (barH >= 14) {
            const lv_font_t *valFont = FontManager::label(barH >= 24 ? 14 : 12);
            lv_obj_t *val = lv_label_create(cont);
            lv_obj_set_style_text_color(val, lv_color_hex(VALUE_TEXT_RGB), 0);
            lv_obj_set_style_text_font(val, valFont, 0);
            lv_obj_set_width(val, W);
            lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
            const int16_t fontH = lv_font_get_line_height(valFont);
            const int16_t valueY = trackY + (barH - fontH) / 2;
            lv_obj_set_pos(val, 0, valueY);
            t->valueLabel = val;
        }

        // Optional widget label — sits in the label band at the user-chosen
        // corner. WidgetLabelOverlay aligns to the container, so we anchor
        // through a transient inner stripe matched to the band.
        if (!noUserLabel) {
            // Build a lightweight inner stripe so WidgetLabelOverlay anchors
            // its label inside the band (rather than the whole widget).
            lv_obj_t *band = lv_obj_create(cont);
            WidgetHelpers::disableInteract(band);
            lv_obj_set_pos(band, 0, bandY);
            lv_obj_set_size(band, W, labelBandH);
            lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(band, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(band, 0, LV_PART_MAIN);
            // Map the user's vertical preference (top/bottom) to the band's
            // inner top — the band itself is already on the right side of the
            // widget. Horizontal alignment (left/center/right) preserved.
            CfgLabelPos innerPos = CfgLabelPos::TOP_LEFT;
            switch (cfg.bar.labelPosition) {
                case CfgLabelPos::TOP_LEFT:
                case CfgLabelPos::BOTTOM_LEFT:
                    innerPos = CfgLabelPos::TOP_LEFT;
                    break;
                case CfgLabelPos::TOP_CENTER:
                case CfgLabelPos::BOTTOM_CENTER:
                    innerPos = CfgLabelPos::TOP_CENTER;
                    break;
                case CfgLabelPos::TOP_RIGHT:
                case CfgLabelPos::BOTTOM_RIGHT:
                    innerPos = CfgLabelPos::TOP_RIGHT;
                    break;
            }
            WidgetLabelOverlay::apply(band, cfg.bar.label, innerPos,
                                      ThemeManager::getEffectiveTextColor());
        }

        // Optional icon (drawn at left edge of the band, before any label)
        if (cfg.bar.iconName[0] != '\0') {
            const char *path = IconAssets::path(cfg.bar.iconName);
            if (path[0] != '\0') {
                lv_obj_t *img = lv_img_create(cont);
                lv_img_set_src(img, path);
                lv_obj_set_style_img_recolor(img, lv_color_hex(SIGNAL_LABEL_RGB), 0);
                lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
                lv_obj_set_pos(img, 1, bandY + 1);
                t->iconImg = img;
            }
        }
    } else {
        // -------------------------------------------------------------------
        // Vertical layout (matches GaugeBarPreview vertical branch)
        // -------------------------------------------------------------------
        const int16_t barW = static_cast<int16_t>(fmaxf(10.0f, W * TRACK_W_RATIO));
        const int16_t padX = (W - barW) / 2;
        // Reserve top for the signal label and bottom for value + suffix.
        const int16_t sigLabelH = H >= 80 ? 14 : 12;
        const int16_t valLabelH = H >= 80 ? 16 : 14;
        const int16_t suffixH = H >= 80 ? 12 : 10;
        const int16_t padTop = sigLabelH + 3;
        const int16_t padBot = valLabelH + suffixH + 6;
        const int16_t trackH = static_cast<int16_t>(fmaxf(MIN_TRACK_DIM, H - padTop - padBot));
        t->trackX = padX;
        t->trackY = padTop;
        t->trackW = barW;
        t->trackH = trackH;

        // Track
        lv_obj_t *track = lv_obj_create(cont);
        lv_obj_set_pos(track, padX, padTop);
        lv_obj_set_size(track, barW, trackH);
        WidgetStyles::applyBarTrack(track);
        lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
        t->track = track;

        // Warning zone — drawn from warningLevel up to dangerLevel (top down).
        if (hasWarn && hasDanger && dangerPct > warnPct) {
            int16_t zY = padTop + static_cast<int16_t>(trackH * (1.0f - dangerPct));
            int16_t zH = static_cast<int16_t>(trackH * (dangerPct - warnPct));
            lv_obj_t *zone = lv_obj_create(cont);
            WidgetHelpers::disableInteract(zone);
            lv_obj_set_pos(zone, padX, zY);
            lv_obj_set_size(zone, barW, zH);
            WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneWarningRgb, ZONE_OPA);
            t->warnZone = zone;
        }

        // Danger zone — band from the top of the track down to dangerLevel.
        if (hasDanger && dangerPct < 1.0f) {
            int16_t topZH = static_cast<int16_t>(trackH * (1.0f - dangerPct));
            if (topZH > 0) {
                lv_obj_t *zone = lv_obj_create(cont);
                WidgetHelpers::disableInteract(zone);
                lv_obj_set_pos(zone, padX, padTop);
                lv_obj_set_size(zone, barW, topZH);
                WidgetStyles::applyBarZone(zone, WidgetHelpers::kZoneDangerRgb, ZONE_OPA);
                t->dangerZone = zone;
            }
        }

        // Fill — sits at the bottom of the track, height grows upwards.
        lv_obj_t *fill = lv_obj_create(cont);
        WidgetHelpers::disableInteract(fill);
        lv_obj_set_pos(fill, padX, padTop + trackH);
        lv_obj_set_size(fill, barW, 0);
        WidgetStyles::applyBarFill(fill, t->primaryRgb);
        t->fill = fill;

        // Signal label — top centre, dropped when the user set a custom label.
        if (cfg.bar.label[0] == '\0') {
            lv_obj_t *sig = lv_label_create(cont);
            lv_label_set_text(sig, sigBuf);
            lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(sig, FontManager::label(sigLabelH), 0);
            lv_obj_align(sig, LV_ALIGN_TOP_MID, 0, 1);
            t->signalLabel = sig;
        }

        // Value — bottom center, large monospace
        lv_obj_t *val = lv_label_create(cont);
        lv_obj_set_style_text_color(val, lv_color_hex(t->primaryRgb), 0);
        lv_obj_set_style_text_font(val, FontManager::label(valLabelH), 0);
        lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -suffixH - 1);
        t->valueLabel = val;

        // Suffix — small line below value
        if (t->suffix[0] != '\0') {
            lv_obj_t *suffix = lv_label_create(cont);
            lv_label_set_text(suffix, t->suffix);
            lv_obj_set_style_text_color(suffix, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(suffix, FontManager::label(suffixH), 0);
            lv_obj_align(suffix, LV_ALIGN_BOTTOM_MID, 0, -1);
            t->suffixLabel = suffix;
        }
    }

    // Mount the alert overlay last so it sits on top of all bar elements. Track
    // the value label so it flips to white during a flash (signalLabel and
    // suffixLabel already use a fixed dim grey — leave them alone for contrast).
    AlertFlash::attach(t->alert, cont);
    if (t->valueLabel)
        AlertFlash::watchLabel(t->alert, t->valueLabel, VALUE_TEXT_RGB);

    WidgetHelpers::attachTagDeleter(cont, t);

    // Initial paint: invalid → 0 (per design).
    BarWidget::update(cont, cfg.bar.minValue, false, cfg);

    LOG_DEBUG("BAR", "Created %s bar '%s' at (%d,%d) size=%dx%d range=[%d,%d]",
              isVertical ? "vertical" : "horizontal", cfg.id, cfg.layout.x, cfg.layout.y + yOffset,
              W, H, static_cast<int>(lroundf(cfg.bar.minValue)),
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
    const float warnPct = !std::isnan(t->warningLevel)
                              ? WidgetHelpers::clampPct(t->warningLevel, t->minValue, t->maxValue)
                              : 1.1f;
    const float dangerPct = !std::isnan(t->dangerLevel)
                                ? WidgetHelpers::clampPct(t->dangerLevel, t->minValue, t->maxValue)
                                : 1.1f;

    // Issue #430: when a ramp is configured the fill colour comes from the
    // ramp at the live value. Without a ramp the legacy three-zone palette
    // (green/orange/red) drives the fill — preserves backward compatibility
    // for configs that haven't opted in.
    const uint32_t fillRgb = t->ramp ? colorAtValue(*t->ramp, displayValue)
                                     : WidgetHelpers::zoneFillColor(pct, warnPct, dangerPct);
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
