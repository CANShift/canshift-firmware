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

constexpr uint32_t kBlinkPeriodMs = 1000;
constexpr lv_opa_t kIdleBgOpa = 0x18;
constexpr int16_t kRootPadPx = 2;
constexpr int16_t kSignalLabelMinH = 28;
constexpr int kSignalLabelFontPx = 10;

enum class WarnState : uint8_t { IDLE, ALARM, STALE };

struct WarningTag {
    lv_obj_t *root;
    lv_obj_t *iconImg;
    lv_obj_t *signalLabel;
    lv_anim_t blinkAnim;
    WarnState state;
    uint32_t bgColor;
    uint32_t labelRgb;
};

void blinkAnimCb(void *target, int32_t v) {
    auto *root = static_cast<lv_obj_t *>(target);
    lv_obj_set_style_bg_opa(root, static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void startBlink(WarningTag *tag) {
    lv_anim_init(&tag->blinkAnim);
    lv_anim_set_var(&tag->blinkAnim, tag->root);
    lv_anim_set_values(&tag->blinkAnim, 0x00, 0xCC);
    lv_anim_set_path_cb(&tag->blinkAnim, lv_anim_path_step);
    lv_anim_set_time(&tag->blinkAnim, kBlinkPeriodMs / 2);
    lv_anim_set_playback_time(&tag->blinkAnim, kBlinkPeriodMs / 2);
    lv_anim_set_repeat_count(&tag->blinkAnim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&tag->blinkAnim, blinkAnimCb);
    lv_anim_start(&tag->blinkAnim);
}

void applyLiveStyle(WarningTag *tag) {
    lv_obj_set_style_border_width(tag->root, 0, LV_PART_MAIN);
    if (tag->iconImg) {
        lv_obj_set_style_img_recolor(tag->iconImg, lv_color_hex(tag->bgColor), 0);
    }
    if (tag->signalLabel) {
        lv_obj_set_style_text_color(tag->signalLabel, lv_color_hex(tag->labelRgb), 0);
    }
}

void applyStaleStyle(WarningTag *tag) {
    const uint32_t staleRgb = ThemeManager::getStaleTextColor();
    lv_obj_set_style_bg_opa(tag->root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag->root, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag->root, lv_color_hex(staleRgb), LV_PART_MAIN);
    if (tag->iconImg) {
        lv_obj_set_style_img_recolor(tag->iconImg, lv_color_hex(staleRgb), 0);
    }
    if (tag->signalLabel) {
        lv_obj_set_style_text_color(tag->signalLabel, lv_color_hex(staleRgb), 0);
    }
}

void enterState(WarningTag *tag, WarnState next) {
    lv_anim_del(tag->root, blinkAnimCb);
    switch (next) {
        case WarnState::ALARM:
            applyLiveStyle(tag);
            startBlink(tag);
            break;
        case WarnState::IDLE:
            applyLiveStyle(tag);
            lv_obj_set_style_bg_opa(tag->root, kIdleBgOpa, LV_PART_MAIN);
            break;
        case WarnState::STALE:
            applyStaleStyle(tag);
            break;
    }
    tag->state = next;
}

lv_obj_t *makeWarningRoot(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *root = lv_obj_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(root, px, py);
    lv_obj_set_size(root, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
    WidgetHelpers::disableInteract(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(cfg.style.criticalColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, kIdleBgOpa, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, kRootPadPx, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, kRootPadPx, LV_PART_MAIN);
    return root;
}

uint32_t dimmedLabelRgb(uint32_t critRgb) {
    return ((critRgb >> 1) & 0x7F7F7F) | 0x404040;
}

lv_obj_t *makeWarningIcon(lv_obj_t *root, const char *iconName, uint32_t critRgb) {
    const void *iconSrc = IconAssets::resolveSource(iconName);
    if (iconSrc == nullptr)
        return nullptr;
    lv_obj_t *iconImg = lv_img_create(root);
    lv_img_set_src(iconImg, iconSrc);
    lv_obj_set_style_img_recolor(iconImg, lv_color_hex(critRgb), 0);
    lv_obj_set_style_img_recolor_opa(iconImg, LV_OPA_COVER, 0);
    return iconImg;
}

lv_obj_t *makeSignalLabel(lv_obj_t *root, const CfgWidget &cfg, uint32_t labelRgb) {
    if (WidgetHelpers::scaledBox(cfg).h < kSignalLabelMinH)
        return nullptr;
    char labelBuf[kCfgMaxSignalLen + 4];
    WidgetHelpers::formatSignalLabel(cfg.signalId, labelBuf, sizeof(labelBuf));
    lv_obj_t *signalLabel = lv_label_create(root);
    lv_label_set_text(signalLabel, labelBuf);
    lv_obj_set_style_text_font(signalLabel, FontManager::label(kSignalLabelFontPx), 0);
    lv_obj_set_style_text_color(signalLabel, lv_color_hex(labelRgb), 0);
    lv_obj_set_style_text_letter_space(signalLabel, 1, 0);
    return signalLabel;
}

}

lv_obj_t *WarningWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *root = makeWarningRoot(parent, cfg, yOffset);
    const uint32_t critRgb = cfg.style.criticalColor.rgb;
    const uint32_t labelRgb = dimmedLabelRgb(critRgb);
    lv_obj_t *iconImg = makeWarningIcon(root, cfg.warning.iconName, critRgb);
    lv_obj_t *signalLabel = makeSignalLabel(root, cfg, labelRgb);
    WidgetTagPool::Slot<WarningTag> tagSlot;
    WarningTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "WARN", root);
    if (!tag)
        return nullptr;
    tag->root = root;
    tag->iconImg = iconImg;
    tag->signalLabel = signalLabel;
    tag->state = WarnState::IDLE;
    tag->bgColor = critRgb;
    tag->labelRgb = labelRgb;
    lv_obj_set_user_data(root, tag);
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

    WarnState next;
    if (!valid) {
        next = WarnState::STALE;
    } else {
        const bool tripped = cfg.warning.invertLogic ? (value < cfg.warning.threshold)
                                                     : (value >= cfg.warning.threshold);
        next = tripped ? WarnState::ALARM : WarnState::IDLE;
    }
    if (next == tag->state)
        return;
    enterState(tag, next);
}
