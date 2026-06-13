#include "warning_widget.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t BLINK_PERIOD_MS = 1000;

struct WarningTag {
    lv_obj_t *root;
    lv_obj_t *iconImg;
    lv_obj_t *signalLabel;
    lv_anim_t blinkAnim;
    bool wasActive;
    uint32_t bgColor;
};

void blinkAnimCb(void *target, int32_t v) {
    auto *root = static_cast<lv_obj_t *>(target);
    lv_obj_set_style_bg_opa(root, static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void startBlink(WarningTag *tag) {
    lv_anim_init(&tag->blinkAnim);
    lv_anim_set_var(&tag->blinkAnim, tag->root);
    // Step path — flash, not pulse.
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
    lv_obj_set_style_bg_opa(tag->root, 0x18, LV_PART_MAIN);
}

} // namespace

lv_obj_t *WarningWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *root = lv_obj_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(root, px, py);
    lv_obj_set_size(root, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
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

    // Glyph fallback was removed in #681 — Orbitron lacks the LVGL symbol range.
    lv_obj_t *iconImg = nullptr;
    const void *iconSrc = IconAssets::resolveSource(cfg.warning.iconName);
    if (iconSrc != nullptr) {
        iconImg = lv_img_create(root);
        lv_img_set_src(iconImg, iconSrc);
        lv_obj_set_style_img_recolor(iconImg, lv_color_hex(critRgb), 0);
        lv_obj_set_style_img_recolor_opa(iconImg, LV_OPA_COVER, 0);
    }

    // TODO(#18): thresholds/font sizes hardcoded for the 320×240 canvas.
    lv_obj_t *signalLabel = nullptr;
    if (cfg.layout.h >= 28) {
        char labelBuf[CFG_MAX_SIGNAL_LEN + 4];
        WidgetHelpers::formatSignalLabel(cfg.signalId, labelBuf, sizeof(labelBuf));
        signalLabel = lv_label_create(root);
        lv_label_set_text(signalLabel, labelBuf);
        lv_obj_set_style_text_font(signalLabel, FontManager::label(10), 0);
        // LVGL doesn't blend text against bg — pre-mix a dimmer composite.
        uint32_t labelRgb = ((critRgb >> 1) & 0x7F7F7F) | 0x404040;
        lv_obj_set_style_text_color(signalLabel, lv_color_hex(labelRgb), 0);
        lv_obj_set_style_text_letter_space(signalLabel, 1, 0);
    }

    // RAII slot guard (#1207) — releases the slot on early-return paths.
    WidgetTagPool::Slot<WarningTag> tagSlot;
    WarningTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("WARN", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(root);
        return nullptr;
    }
    tag->root = root;
    tag->iconImg = iconImg;
    tag->signalLabel = signalLabel;
    tag->wasActive = false;
    tag->bgColor = critRgb;
    lv_obj_set_user_data(root, tag);
    // Wipes pending anims before slot release — closes the queued-anim race (#886).
    WidgetHelpers::attachTagDeleter(root, tagSlot.commit());

    LOG_DEBUG("WARN", "Created warning '%s' icon='%s' (%s)", cfg.id, cfg.warning.iconName,
              iconImg ? "asset" : "none");
    return root;
}

void WarningWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<WarningTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    // Missing/timed-out → blink. Calm widget would hide ECU/wiring faults.
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
