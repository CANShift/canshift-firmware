#include "shift_light_widget.h"
#include "config/config_loader.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>

namespace {

constexpr uint8_t kSegmentCount = 12;
constexpr int16_t kSegmentGapPx = 3;
constexpr uint32_t kTrackRgb = 0x222222;

struct ShiftLightTag {
    lv_obj_t *segments[kSegmentCount];
    float startValue;
    float fullValue;
    uint8_t redFromIndex;
    uint8_t lastLit;
    uint32_t litRgb;
    bool lastValid;
};

float resolveFullValue(const CfgShiftLightParams &params) {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (dash.loaded && dash.revLimitRpm > params.startValue)
        return dash.revLimitRpm;
    return params.startValue + 1.0f;
}

uint8_t litSegments(float value, const ShiftLightTag &tag) {
    if (!(tag.fullValue > tag.startValue))
        return 0;
    const float pct = (value - tag.startValue) / (tag.fullValue - tag.startValue);
    if (pct <= 0.0f)
        return 0;
    if (pct >= 1.0f)
        return kSegmentCount;
    return static_cast<uint8_t>(pct * kSegmentCount);
}

void paintSegments(ShiftLightTag *tag, uint8_t lit) {
    for (uint8_t i = 0; i < kSegmentCount; ++i) {
        const uint32_t rgb = (i >= lit)                 ? kTrackRgb
                             : (i >= tag->redFromIndex) ? WidgetHelpers::kZoneDangerRgb
                                                        : tag->litRgb;
        lv_obj_set_style_bg_color(tag->segments[i], lv_color_hex(rgb), 0);
    }
    tag->lastLit = lit;
}

} // namespace

lv_obj_t *ShiftLightWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    WidgetTagPool::Slot<ShiftLightTag> tagSlot;
    ShiftLightTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("SHIFT", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }

    const int16_t w = cfg.layout.w;
    const int16_t h = cfg.layout.h;
    const int16_t segW =
        static_cast<int16_t>((w - kSegmentGapPx * (kSegmentCount - 1)) / kSegmentCount);
    const int16_t usedW =
        static_cast<int16_t>(segW * kSegmentCount + kSegmentGapPx * (kSegmentCount - 1));
    const int16_t x0 = static_cast<int16_t>((w - usedW) / 2);

    tag->startValue = cfg.shiftLight.startValue;
    tag->fullValue = resolveFullValue(cfg.shiftLight);
    tag->redFromIndex = static_cast<uint8_t>(cfg.shiftLight.redSegments >= kSegmentCount
                                                 ? 0
                                                 : kSegmentCount - cfg.shiftLight.redSegments);
    tag->litRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    tag->lastLit = 0xFF;
    tag->lastValid = false;

    for (uint8_t i = 0; i < kSegmentCount; ++i) {
        lv_obj_t *seg = lv_obj_create(cont);
        lv_obj_set_size(seg, segW, h);
        lv_obj_set_pos(seg, static_cast<int16_t>(x0 + i * (segW + kSegmentGapPx)), 0);
        WidgetHelpers::resetContainerStyle(seg);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(kTrackRgb), 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        tag->segments[i] = seg;
    }
    paintSegments(tag, 0);

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<ShiftLightTag>, LV_EVENT_DELETE,
                        tagSlot.commit());
    return cont;
}

void ShiftLightWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    (void)cfg;
    if (!obj)
        return;
    auto *tag = static_cast<ShiftLightTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    if (!valid) {
        if (tag->lastValid || tag->lastLit != 0) {
            paintSegments(tag, 0);
            tag->lastValid = false;
        }
        return;
    }
    tag->lastValid = true;

    const uint8_t lit = litSegments(value, *tag);
    if (lit != tag->lastLit)
        paintSegments(tag, lit);
}

void ShiftLightWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<ShiftLightTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;
    tag->litRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    paintSegments(tag, tag->lastLit == 0xFF ? 0 : tag->lastLit);
}
