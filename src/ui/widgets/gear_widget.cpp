
#include "gear_widget.h"
#include "ui/font_manager.h"
#include "ui/widget_label.h"
#include "ui/theme_manager.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

namespace {

struct GearTag {
    lv_obj_t *label;
    uint32_t lastColorRgb;
};

constexpr int16_t kSigHeaderH = 14;

const lv_font_t *selectFont(const CfgWidget &cfg, uint8_t &sizeOut) {
    if (cfg.label.big > 0) {
        sizeOut = WidgetHelpers::deviceFontPxForBig(cfg.label.big);
        return FontManager::value(sizeOut);
    }
    const int byHeight = (cfg.layout.h * 85) / 100;
    const int byWidth = (cfg.layout.w * 72) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 10)
        s = 10;
    if (s > 48)
        s = 48;
    const uint8_t size = static_cast<uint8_t>(s);
    sizeOut = size;
    if (size >= 17)
        return FontManager::value(size);
    return FontManager::units();
}

} // namespace

lv_obj_t *GearWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    const int16_t sigHeaderH = 0;
    const int16_t digitBandH = cfg.layout.h;
    (void)kSigHeaderH;

    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, static_cast<int16_t>(sigHeaderH / 2));

    uint8_t fontSize = 0;
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, selectFont(cfg, fontSize), 0);
    lv_label_set_text(label, "N");

    const bool primaryTier = fontSize >= WidgetHelpers::kRulePrimaryFontMin;
    WidgetHelpers::makeTopRule(
        cont, primaryTier ? WidgetHelpers::kRulePrimaryPx : WidgetHelpers::kRuleSecondaryPx,
        primaryTier ? textRgb : WidgetHelpers::kTrackRgb);
    WidgetLabelOverlay::applySignalHeader(cont, cfg.signalId);

    WidgetTagPool::Slot<GearTag> tagSlot;
    GearTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("GEAR", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    tag->label = label;
    tag->lastColorRgb = textRgb;
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GearTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
}

void GearWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GearTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->label)
        return;

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    WidgetStyles::setTextColorIfChanged(tag->label, tag->lastColorRgb, textRgb);
}

void GearWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GearTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->label)
        return;
    lv_obj_t *label = tag->label;

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    if (!valid) {
        WidgetHelpers::setLabelTextIfChanged(label, "N");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb,
                                            ThemeManager::getStaleTextColor());
        return;
    }

    int32_t gear = static_cast<int32_t>(value);
    if (gear == 0) {
        WidgetHelpers::setLabelTextIfChanged(label, "N");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, textRgb);
        return;
    }
    if (gear < 0) {
        WidgetHelpers::setLabelTextIfChanged(label, "R");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, cfg.style.warningColor.rgb);
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gear);
    WidgetHelpers::setLabelTextIfChanged(label, buf);
    WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, textRgb);
}
