// bar_widget.cpp — Horizontal / vertical bar widget that mirrors studio's
// GaugeBarPreview: a proportional track with translucent warning/danger
// zones, a threshold-coloured fill, signal label, value readout, and an
// optional widget label at a chosen corner.

#include "bar_widget.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/widget_label.h"
#include "diag/logger.h"

#include <ctype.h>
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
    lv_obj_t *valueLabel; // nullable on very small widgets
    lv_obj_t *suffixLabel; // nullable — shown below value for vertical layout
    lv_obj_t *widgetLabel; // nullable
    lv_obj_t *iconImg;     // nullable
    bool isVertical;
    int16_t trackX, trackY, trackW, trackH;
    float minValue, maxValue;
    float warningLevel, dangerLevel;
    uint32_t primaryRgb, warningRgb, criticalRgb;
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[8];
    float lastValue;
    bool wasValid;
};

void formatSignalLabel(const char *src, char *out, size_t outLen) {
    if (outLen == 0) return;
    if (!src || src[0] == '\0') {
        strlcpy(out, "-", outLen);
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < outLen; ++i) {
        char c = src[i];
        if (c == '_') c = ' ';
        out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    out[j] = '\0';
}

float clampPct(float v, float minV, float maxV) {
    if (maxV <= minV) return 0.0f;
    float pct = (v - minV) / (maxV - minV);
    if (pct < 0.0f) return 0.0f;
    if (pct > 1.0f) return 1.0f;
    return pct;
}

// Studio uses a faint grey for label text when dimmed. We use the same fixed
// tone so the bar reads identically across themes.
constexpr uint32_t SIGNAL_LABEL_RGB = 0x888888;
constexpr uint32_t TRACK_BG_RGB = 0x1C1C1C;
constexpr uint32_t TICK_LABEL_RGB = 0x383838;
constexpr uint32_t VALUE_TEXT_RGB = 0xFFFFFF;  // value % always white per user spec
constexpr lv_opa_t ZONE_OPA = 0x35;

// Fixed automotive zone palette — fill colour reflects the value's zone rather
// than `style.primaryColor`. Drivers expect green/orange/red semantics
// regardless of how the widget's brand colour is set in studio.
constexpr uint32_t ZONE_NORMAL_RGB  = 0x00CC44; // green
constexpr uint32_t ZONE_WARNING_RGB = 0xFF8800; // orange
constexpr uint32_t ZONE_DANGER_RGB  = 0xFF4444; // red

uint32_t zoneFillColor(float pct, float warnPct, float dangerPct) {
    if (pct >= dangerPct) return ZONE_DANGER_RGB;
    if (pct >= warnPct)   return ZONE_WARNING_RGB;
    return ZONE_NORMAL_RGB;
}

void renderValueText(BarTag *t, float v) {
    if (!t->valueLabel) return;
    char buf[24];
    const char *suffix = t->isVertical ? "" : t->suffix;
    snprintf(buf, sizeof(buf), "%s%.*f%s", t->prefix, t->decimalPlaces, v, suffix);
    lv_label_set_text(t->valueLabel, buf);
}

} // namespace

lv_obj_t *BarWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    const bool isVertical = cfg.bar.isVertical;
    const int16_t W = cfg.layout.w;
    const int16_t H = cfg.layout.h;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, W, H);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    if (cfg.style.hasBorder) {
        lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cont, lv_color_hex(cfg.style.borderColor.rgb), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    }

    auto *t = new BarTag{};
    t->isVertical = isVertical;
    t->minValue = cfg.bar.minValue;
    t->maxValue = cfg.bar.maxValue;
    t->warningLevel = cfg.bar.warningLevel;
    t->dangerLevel = cfg.bar.dangerLevel;
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

    const bool hasWarn = !std::isnan(t->warningLevel) && t->warningLevel > t->minValue;
    const bool hasDanger = !std::isnan(t->dangerLevel) && t->dangerLevel > t->minValue
                           && t->dangerLevel <= t->maxValue;
    const float warnPct = hasWarn ? clampPct(t->warningLevel, t->minValue, t->maxValue) : 1.0f;
    const float dangerPct = hasDanger ? clampPct(t->dangerLevel, t->minValue, t->maxValue) : 1.0f;

    char sigBuf[CFG_MAX_SIGNAL_LEN + 4];
    formatSignalLabel(cfg.signalId, sigBuf, sizeof(sigBuf));

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
        const bool labelIsTop = noUserLabel
                                || cfg.bar.labelPosition == CfgLabelPos::TOP_LEFT
                                || cfg.bar.labelPosition == CfgLabelPos::TOP_CENTER
                                || cfg.bar.labelPosition == CfgLabelPos::TOP_RIGHT;

        // Reserve a label band: 25 % of widget height, clamped 14..24. The
        // 14-px floor matches Montserrat 12's line height — anything tighter
        // visibly clips the value (\"65%\") and the signal name. The 24-px
        // cap keeps the bar dominant on tall widgets.
        int16_t labelBandH = static_cast<int16_t>((H * 25) / 100);
        if (labelBandH < 14) labelBandH = 14;
        if (labelBandH > 24) labelBandH = 24;
        // Bar fills the rest minus a small gap.
        const int16_t gap = 2;
        const int16_t barH = H - labelBandH - gap;
        const int16_t trackY = labelIsTop ? labelBandH + gap : 0;
        const int16_t bandY  = labelIsTop ? 0 : barH + gap;
        const int16_t trackW = W - HORIZ_PAD_X * 2;
        t->trackX = HORIZ_PAD_X;
        t->trackY = trackY;
        t->trackW = trackW;
        t->trackH = barH;

        // Track background — square corners per user spec
        lv_obj_t *track = lv_obj_create(cont);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(track, HORIZ_PAD_X, trackY);
        lv_obj_set_size(track, trackW, barH);
        lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(track, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
        t->track = track;

        // Warning zone (warning..danger band)
        if (hasWarn && hasDanger && dangerPct > warnPct) {
            int16_t zX = HORIZ_PAD_X + static_cast<int16_t>(trackW * warnPct);
            int16_t zW = static_cast<int16_t>(trackW * (dangerPct - warnPct));
            lv_obj_t *zone = lv_obj_create(cont);
            lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(zone, zX, trackY);
            lv_obj_set_size(zone, zW, barH);
            lv_obj_set_style_bg_color(zone, lv_color_hex(ZONE_WARNING_RGB), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(zone, ZONE_OPA, LV_PART_MAIN);
            lv_obj_set_style_radius(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(zone, 0, LV_PART_MAIN);
            t->warnZone = zone;
        }

        // Danger zone (danger..max band)
        if (hasDanger && dangerPct < 1.0f) {
            int16_t zX = HORIZ_PAD_X + static_cast<int16_t>(trackW * dangerPct);
            int16_t zW = static_cast<int16_t>(trackW * (1.0f - dangerPct));
            lv_obj_t *zone = lv_obj_create(cont);
            lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(zone, zX, trackY);
            lv_obj_set_size(zone, zW, barH);
            lv_obj_set_style_bg_color(zone, lv_color_hex(ZONE_DANGER_RGB), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(zone, ZONE_OPA, LV_PART_MAIN);
            lv_obj_set_style_radius(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(zone, 0, LV_PART_MAIN);
            t->dangerZone = zone;
        }

        // Fill — width is updated dynamically. Starts at 0. Colour set in
        // update() based on the active zone (green/orange/red).
        lv_obj_t *fill = lv_obj_create(cont);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(fill, HORIZ_PAD_X, trackY);
        lv_obj_set_size(fill, 0, barH);
        lv_obj_set_style_bg_color(fill, lv_color_hex(ZONE_NORMAL_RGB), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
        t->fill = fill;

        // Signal label — only when the user hasn't supplied a custom one.
        // Lives in the label band, NOT over the track.
        if (noUserLabel) {
            lv_obj_t *sig = lv_label_create(cont);
            lv_label_set_text(sig, sigBuf);
            lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(sig, FontManager::get(12), 0);
            lv_obj_set_style_text_letter_space(sig, 1, 0);
            lv_obj_set_pos(sig, 2, bandY + 1);
            t->signalLabel = sig;
        }

        // Value label — white, centred ON the bar track (not the label band).
        // Sits over the fill so it reads as part of the bar.
        if (barH >= 14) {
            const lv_font_t *valFont = FontManager::get(barH >= 24 ? 14 : 12);
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
            lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
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
                case CfgLabelPos::BOTTOM_LEFT:    innerPos = CfgLabelPos::TOP_LEFT; break;
                case CfgLabelPos::TOP_CENTER:
                case CfgLabelPos::BOTTOM_CENTER:  innerPos = CfgLabelPos::TOP_CENTER; break;
                case CfgLabelPos::TOP_RIGHT:
                case CfgLabelPos::BOTTOM_RIGHT:   innerPos = CfgLabelPos::TOP_RIGHT; break;
            }
            WidgetLabelOverlay::apply(band, cfg.bar.label, innerPos,
                                       cfg.style.textColor.rgb);
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
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(track, padX, padTop);
        lv_obj_set_size(track, barW, trackH);
        lv_obj_set_style_bg_color(track, lv_color_hex(TRACK_BG_RGB), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(track, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
        t->track = track;

        // Warning zone — drawn from warningLevel up to dangerLevel (top down).
        if (hasWarn && hasDanger && dangerPct > warnPct) {
            int16_t zY = padTop + static_cast<int16_t>(trackH * (1.0f - dangerPct));
            int16_t zH = static_cast<int16_t>(trackH * (dangerPct - warnPct));
            lv_obj_t *zone = lv_obj_create(cont);
            lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(zone, padX, zY);
            lv_obj_set_size(zone, barW, zH);
            lv_obj_set_style_bg_color(zone, lv_color_hex(ZONE_WARNING_RGB), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(zone, ZONE_OPA, LV_PART_MAIN);
            lv_obj_set_style_radius(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(zone, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(zone, 0, LV_PART_MAIN);
            t->warnZone = zone;
        }

        // Danger zone — band from the top of the track down to dangerLevel.
        if (hasDanger && dangerPct < 1.0f) {
            int16_t topZH = static_cast<int16_t>(trackH * (1.0f - dangerPct));
            if (topZH > 0) {
                lv_obj_t *zone = lv_obj_create(cont);
                lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_pos(zone, padX, padTop);
                lv_obj_set_size(zone, barW, topZH);
                lv_obj_set_style_bg_color(zone, lv_color_hex(ZONE_DANGER_RGB), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(zone, ZONE_OPA, LV_PART_MAIN);
                lv_obj_set_style_radius(zone, 0, LV_PART_MAIN);
                lv_obj_set_style_border_width(zone, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(zone, 0, LV_PART_MAIN);
                t->dangerZone = zone;
            }
        }

        // Fill — sits at the bottom of the track, height grows upwards.
        lv_obj_t *fill = lv_obj_create(cont);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(fill, padX, padTop + trackH);
        lv_obj_set_size(fill, barW, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(t->primaryRgb), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
        t->fill = fill;

        // Signal label — top centre, dropped when the user set a custom label.
        if (cfg.bar.label[0] == '\0') {
            lv_obj_t *sig = lv_label_create(cont);
            lv_label_set_text(sig, sigBuf);
            lv_obj_set_style_text_color(sig, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(sig, FontManager::get(sigLabelH), 0);
            lv_obj_align(sig, LV_ALIGN_TOP_MID, 0, 1);
            t->signalLabel = sig;
        }

        // Value — bottom center, large monospace
        lv_obj_t *val = lv_label_create(cont);
        lv_obj_set_style_text_color(val, lv_color_hex(t->primaryRgb), 0);
        lv_obj_set_style_text_font(val, FontManager::get(valLabelH), 0);
        lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -suffixH - 1);
        t->valueLabel = val;

        // Suffix — small line below value
        if (t->suffix[0] != '\0') {
            lv_obj_t *suffix = lv_label_create(cont);
            lv_label_set_text(suffix, t->suffix);
            lv_obj_set_style_text_color(suffix, lv_color_hex(SIGNAL_LABEL_RGB), 0);
            lv_obj_set_style_text_font(suffix, FontManager::get(suffixH), 0);
            lv_obj_align(suffix, LV_ALIGN_BOTTOM_MID, 0, -1);
            t->suffixLabel = suffix;
        }
    }

    lv_obj_set_user_data(cont, t);
    lv_obj_add_event_cb(cont,
                        [](lv_event_t *e) {
                            auto *p = static_cast<BarTag *>(lv_event_get_user_data(e));
                            delete p;
                        },
                        LV_EVENT_DELETE, t);

    // Initial paint: invalid → 0 (per design).
    BarWidget::update(cont, cfg.bar.minValue, false, cfg);

    LOG_DEBUG("BAR", "Created %s bar '%s' at (%d,%d) size=%dx%d range=[%.0f,%.0f]",
              isVertical ? "vertical" : "horizontal", cfg.id, cfg.layout.x,
              cfg.layout.y + yOffset, W, H, cfg.bar.minValue, cfg.bar.maxValue);
    return cont;
}

void BarWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj) return;
    auto *t = static_cast<BarTag *>(lv_obj_get_user_data(obj));
    if (!t || !t->fill || !t->valueLabel) return;

    // When the signal is missing, studio shows the "—" placeholder. The user
    // wants 0 instead so the dash always reads numerically.
    const float displayValue = valid ? value : 0.0f;

    // Skip redundant updates (NaN sentinel forces the first paint through).
    if (valid == t->wasValid && !std::isnan(t->lastValue) && displayValue == t->lastValue)
        return;
    t->lastValue = displayValue;
    t->wasValid = valid;

    const float pct = clampPct(displayValue, t->minValue, t->maxValue);
    const float warnPct = !std::isnan(t->warningLevel)
                              ? clampPct(t->warningLevel, t->minValue, t->maxValue)
                              : 1.1f;
    const float dangerPct = !std::isnan(t->dangerLevel)
                                ? clampPct(t->dangerLevel, t->minValue, t->maxValue)
                                : 1.1f;

    const uint32_t fillRgb = zoneFillColor(pct, warnPct, dangerPct);
    lv_obj_set_style_bg_color(t->fill, lv_color_hex(fillRgb), LV_PART_MAIN);

    if (!t->isVertical) {
        int16_t fillW = static_cast<int16_t>(t->trackW * pct);
        lv_obj_set_size(t->fill, fillW, t->trackH);
    } else {
        int16_t fillH = static_cast<int16_t>(t->trackH * pct);
        lv_obj_set_pos(t->fill, t->trackX, t->trackY + t->trackH - fillH);
        lv_obj_set_size(t->fill, t->trackW, fillH);
    }

    if (t->valueLabel) {
        lv_obj_set_style_text_color(t->valueLabel, lv_color_hex(fillRgb), 0);
        renderValueText(t, displayValue);
    }

    (void)cfg;
}
