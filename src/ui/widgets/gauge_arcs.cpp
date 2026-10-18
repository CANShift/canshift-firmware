#include "gauge_arcs.h"
#include "ui/theme_manager.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"

#include <algorithm>

using GaugeArcs::kArcYShift;

namespace {

constexpr float kArcSweep = 270.0f;
constexpr uint16_t kArcSweepInt = 270;
constexpr uint16_t kArcRotation = 135;

constexpr int32_t kMinArcDiam = 40;
constexpr int32_t kArcContainerPadding = 4;

lv_obj_t *createSectorArc(lv_obj_t *parent, int32_t diam, uint16_t startAngle, uint16_t endAngle,
                          uint32_t colorRgb, uint8_t arcWidth) {
    if (startAngle >= endAngle)
        return nullptr;

    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, kArcYShift);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, kArcRotation);
    lv_arc_set_bg_angles(arc, startAngle, endAngle);
    lv_arc_set_angles(arc, 0, 0);

    lv_obj_set_style_arc_color(arc, lv_color_hex(colorRgb), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

lv_obj_t *createValueArc(lv_obj_t *parent, int32_t diam, uint8_t indicatorWidth) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diam, diam);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, kArcYShift);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(arc, kArcRotation);
    lv_arc_set_bg_angles(arc, 0, kArcSweepInt);
    lv_arc_set_angles(arc, 0, 0);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFFu), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, indicatorWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    WidgetStyles::disableArcKnob(arc);

    return arc;
}

}

uint8_t GaugeArcs::strokeWidthFor(const CfgWidget &cfg) {
    const WidgetHelpers::ScaledBox box = WidgetHelpers::scaledBox(cfg);
    const float r = std::min(static_cast<float>(box.w) * 0.48f, static_cast<float>(box.h) * 0.49f);
    const float w = r * 0.30f;
    return static_cast<uint8_t>(w < 5.0f ? 5.0f : w);
}

uint16_t GaugeArcs::valueToAngle(float value, float minVal, float maxVal) {
    return static_cast<uint16_t>(WidgetHelpers::clampPct(value, minVal, maxVal) * kArcSweep);
}

void GaugeArcs::angleAnimCb(void *obj, int32_t angle) {
    lv_arc_set_angles(static_cast<lv_obj_t *>(obj), 0, static_cast<uint16_t>(angle));
}

int32_t GaugeArcs::diameterFor(const CfgWidget &cfg) {
    int32_t diam = WidgetHelpers::scaledSquare(cfg) - kArcContainerPadding;
    if (diam < kMinArcDiam)
        diam = kMinArcDiam;
    return diam;
}

void GaugeArcs::buildBackgroundTrack(lv_obj_t *cont, int32_t diam, uint8_t strokeW) {
    createSectorArc(cont, diam, 0, kArcSweepInt, ThemeManager::trackColor(), strokeW);
}

lv_obj_t *GaugeArcs::buildValueFill(lv_obj_t *cont, int32_t diam, uint32_t inkRgb,
                                    uint8_t strokeW) {
    lv_obj_t *fillArc = createValueArc(cont, diam, strokeW);
    lv_arc_set_angles(fillArc, 0, 0);
    lv_obj_set_style_arc_color(fillArc, lv_color_hex(inkRgb), LV_PART_INDICATOR);
    return fillArc;
}
