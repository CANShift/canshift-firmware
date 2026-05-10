// warning_widget.cpp — Threshold-driven alert with icon + signal label.
// Mirrors studio's WarningPreview (canshift-studio/src/components/editor/
// WidgetPreview.tsx) at runtime: translucent critical background that blinks
// when active, an icon at the top, and the signal name underneath.

#include "warning_widget.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widgets/widget_helpers.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t BLINK_PERIOD_MS = 1000;

struct WarningTag {
    lv_obj_t *root;      // colored background container
    lv_obj_t *iconImg;   // nullptr if asset missing — falls back to glyph label
    lv_obj_t *iconLabel; // nullptr unless the glyph fallback is used
    lv_obj_t *signalLabel;
    lv_anim_t blinkAnim;
    bool wasActive;
    uint32_t bgColor; // criticalColor (RGB)
};

void blinkAnimCb(void *target, int32_t v) {
    auto *root = static_cast<lv_obj_t *>(target);
    lv_obj_set_style_bg_opa(root, static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void startBlink(WarningTag *tag) {
    lv_anim_init(&tag->blinkAnim);
    lv_anim_set_var(&tag->blinkAnim, tag->root);
    // Hard ON/OFF — step path makes the value snap rather than ease, so the
    // widget actually flashes instead of pulsing softly. 0x00 fully clears the
    // bg so the alarm reads as a real blink, not a glow.
    lv_anim_set_values(&tag->blinkAnim, 0x00, 0xCC);
    lv_anim_set_path_cb(&tag->blinkAnim, lv_anim_path_step);
    lv_anim_set_time(&tag->blinkAnim, BLINK_PERIOD_MS / 2);
    lv_anim_set_playback_time(&tag->blinkAnim, BLINK_PERIOD_MS / 2);
    lv_anim_set_repeat_count(&tag->blinkAnim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&tag->blinkAnim, blinkAnimCb);
    lv_anim_start(&tag->blinkAnim);
}

void stopBlink(WarningTag *tag) {
    lv_anim_del(tag->root, blinkAnimCb);
    // When inactive: keep a faint bg so the widget remains visible
    lv_obj_set_style_bg_opa(tag->root, 0x18, LV_PART_MAIN);
}

} // namespace

lv_obj_t *WarningWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_pos(root, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(root, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(cfg.style.criticalColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, 0x18, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 2, LV_PART_MAIN);

    const uint32_t critRgb = cfg.style.criticalColor.rgb;

    // Try the .bin asset first; fall back to a glyph if missing/empty.
    lv_obj_t *iconImg = nullptr;
    lv_obj_t *iconLabel = nullptr;
    const char *assetPath = IconAssets::path(cfg.warning.iconName);
    if (assetPath[0] != '\0') {
        iconImg = lv_img_create(root);
        lv_img_set_src(iconImg, assetPath);
        lv_obj_set_style_img_recolor(iconImg, lv_color_hex(critRgb), 0);
        lv_obj_set_style_img_recolor_opa(iconImg, LV_OPA_COVER, 0);
    } else {
        iconLabel = lv_label_create(root);
        lv_label_set_text(iconLabel, IconAssets::fallbackGlyph(cfg.warning.iconName));
        // Sized so icon + signal label both fit in the widget height.
        uint8_t iconSize = cfg.layout.h >= 80   ? 32
                           : cfg.layout.h >= 56 ? 24
                           : cfg.layout.h >= 40 ? 20
                           : cfg.layout.h >= 32 ? 16
                                                : 12;
        // Warning icon is the attention-getter — pick the heaviest weight that
        // fits the requested size (primary at ≥32, secondary at 20–28, label
        // for the tiny 12/16 px cells).
        const lv_font_t *iconFont = (iconSize >= 32)   ? FontManager::primary(iconSize)
                                    : (iconSize >= 20) ? FontManager::secondary(iconSize)
                                                       : FontManager::label(iconSize);
        lv_obj_set_style_text_font(iconLabel, iconFont, 0);
        lv_obj_set_style_text_color(iconLabel, lv_color_hex(critRgb), 0);
    }

    // Signal label below the icon — replaced by the user-configured corner
    // label (handled by WidgetLabelOverlay below) when one is set, and dropped
    // entirely on very small layouts where there is no room for it.
    lv_obj_t *signalLabel = nullptr;
    if (cfg.warning.label[0] == '\0' && cfg.layout.h >= 28) {
        char labelBuf[CFG_MAX_SIGNAL_LEN + 4];
        WidgetHelpers::formatSignalLabel(cfg.signalId, labelBuf, sizeof(labelBuf));
        signalLabel = lv_label_create(root);
        lv_label_set_text(signalLabel, labelBuf);
        const uint8_t sigFontSize = cfg.layout.h >= 56 ? 14 : 12;
        lv_obj_set_style_text_font(signalLabel, FontManager::label(sigFontSize), 0);
        // Studio uses criticalColor + 99 alpha — at runtime LVGL doesn't blend
        // the text against the bg, so use a dimmer composite tone instead.
        uint32_t labelRgb = ((critRgb >> 1) & 0x7F7F7F) | 0x404040;
        lv_obj_set_style_text_color(signalLabel, lv_color_hex(labelRgb), 0);
        lv_obj_set_style_text_letter_space(signalLabel, 1, 0);
    }

    auto *tag = new WarningTag{root, iconImg, iconLabel, signalLabel, lv_anim_t{}, false, critRgb};
    lv_obj_set_user_data(root, tag);
    lv_obj_add_event_cb(
        root,
        [](lv_event_t *e) {
            auto *t = static_cast<WarningTag *>(lv_event_get_user_data(e));
            if (t) {
                lv_anim_del(t->root, blinkAnimCb);
                delete t;
            }
        },
        LV_EVENT_DELETE, tag);

    // Optional widget label drawn at the configured corner.
    WidgetLabelOverlay::apply(root, cfg.warning.label, cfg.warning.labelPosition,
                              ThemeManager::getEffectiveTextColor());

    LOG_DEBUG("WARN", "Created warning '%s' icon='%s' (%s)", cfg.id, cfg.warning.iconName,
              iconImg ? "asset" : "glyph");
    return root;
}

void WarningWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<WarningTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    // A missing/timed-out signal is treated as an active alert: the driver
    // should know when a fault flag stops reporting (wiring break, ECU drop)
    // rather than silently see a calm widget. Same blink as a tripped alert.
    bool active;
    if (!valid) {
        active = true;
    } else {
        active = cfg.warning.invertLogic ? (value < cfg.warning.threshold)
                                         : (value >= cfg.warning.threshold);
    }
    if (active == tag->wasActive)
        return;
    tag->wasActive = active;

    if (active) {
        startBlink(tag);
    } else {
        stopBlink(tag);
    }
}
