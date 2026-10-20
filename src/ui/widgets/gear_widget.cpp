
#include "gear_widget.h"
#include "ui/widget_label.h"
#include "ui/theme_manager.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/value_cluster.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

namespace {

constexpr const char *kStaleGear = "-";

struct GearTag {
    lv_obj_t *label;
    uint32_t lastColorRgb;
};

uint8_t selectFontPx(const CfgWidget &cfg) {
    if (cfg.label.big > 0)
        return WidgetHelpers::deviceFontPxForBig(cfg.label.big);
    const WidgetHelpers::ScaledBox box = WidgetHelpers::scaledBox(cfg);
    const int byHeight = (box.h * 85) / 100;
    const int byWidth = (box.w * 72) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 10)
        s = 10;
    if (s > 48)
        s = 48;
    return static_cast<uint8_t>(s);
}

void showGear(lv_obj_t *label, GearTag *tag, const char *text, uint32_t rgb) {
    WidgetHelpers::setLabelTextIfChanged(label, text);
    WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, rgb);
}

lv_obj_t *buildGearParts(lv_obj_t *cont, const CfgWidget &cfg, uint32_t textRgb) {
    const uint8_t fontSize = selectFontPx(cfg);
    lv_obj_t *label = ValueCluster::makeValue(ValueCluster::makeRow(cont), fontSize);
    if (!label) {
        LOG_ERROR("GEAR", "value label create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    lv_label_set_text(label, kStaleGear);
    ValueCluster::makeRule(cont, fontSize, textRgb);
    WidgetLabelOverlay::applyWidgetHeader(cont, cfg.label.kicker, cfg.signalId);
    return label;
}

}

lv_obj_t *GearWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    lv_obj_t *label = buildGearParts(cont, cfg, textRgb);
    if (!label)
        return nullptr;

    WidgetTagPool::Slot<GearTag> tagSlot;
    GearTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "GEAR", cont);
    if (!tag)
        return nullptr;
    tag->label = label;
    tag->lastColorRgb = ThemeManager::getStaleTextColor();
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GearTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    return cont;
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
        showGear(label, tag, kStaleGear, ThemeManager::getStaleTextColor());
        return;
    }

    int32_t gear = static_cast<int32_t>(value);
    if (gear == 0) {
        showGear(label, tag, "N", textRgb);
        return;
    }
    if (gear < 0) {
        showGear(label, tag, "R", cfg.style.warningColor.rgb);
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gear);
    showGear(label, tag, buf, textRgb);
}
