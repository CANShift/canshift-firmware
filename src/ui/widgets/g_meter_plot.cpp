#include "g_meter_plot.h"

#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"

namespace GMeterPlot {

namespace {

using GMeterMetrics::kDotSidePx;
using GMeterMetrics::kStrokePx;

constexpr int16_t kUnplacedPx = INT16_MIN;

enum Guide : uint8_t { OuterRing, InnerRing, HorizontalAxis, VerticalAxis };

lv_obj_t *makeBareObject(lv_obj_t *parent, int16_t w, int16_t h) {
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj)
        return nullptr;
    WidgetHelpers::resetContainerStyle(obj);
    lv_obj_set_size(obj, w, h);
    return obj;
}

lv_obj_t *makeRing(lv_obj_t *plot, int16_t diameter) {
    lv_obj_t *ring = makeBareObject(plot, diameter, diameter);
    if (!ring)
        return nullptr;
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, kStrokePx, LV_PART_MAIN);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
    return ring;
}

lv_obj_t *makeAxis(lv_obj_t *plot, int16_t w, int16_t h) {
    lv_obj_t *axis = makeBareObject(plot, w, h);
    if (!axis)
        return nullptr;
    lv_obj_set_style_bg_opa(axis, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(axis, LV_ALIGN_CENTER, 0, 0);
    return axis;
}

lv_obj_t *makeEnvelope(lv_obj_t *plot) {
    lv_obj_t *line = lv_line_create(plot);
    if (!line)
        return nullptr;
    lv_obj_set_style_line_width(line, kStrokePx, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(ThemeManager::dimColor()), LV_PART_MAIN);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

lv_obj_t *makeDot(lv_obj_t *plot) {
    lv_obj_t *dot = makeBareObject(plot, kDotSidePx, kDotSidePx);
    if (!dot)
        return nullptr;
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

bool buildGuides(lv_obj_t *plot, int16_t side, Plot &out) {
    out.guides[OuterRing] = makeRing(plot, side);
    out.guides[InnerRing] =
        makeRing(plot, static_cast<int16_t>(side / GMeterMetrics::kInnerRingDivisor));
    out.guides[HorizontalAxis] = makeAxis(plot, side, kStrokePx);
    out.guides[VerticalAxis] = makeAxis(plot, kStrokePx, side);
    for (lv_obj_t *guide : out.guides) {
        if (!guide)
            return false;
    }
    return true;
}

void placeDot(const Plot &plot) {
    const int16_t centre = static_cast<int16_t>(plot.radius - kDotSidePx / 2);
    lv_obj_set_pos(plot.dot, static_cast<int16_t>(centre + plot.dotDx),
                   static_cast<int16_t>(centre + plot.dotDy));
}

bool updateVertices(Plot &plot, const ImuCoreState &core, float rangeG) {
    bool changed = false;
    for (uint8_t sector = 0; sector < kImuSectorCount; ++sector) {
        int16_t dx = 0;
        int16_t dy = 0;
        imu_core_envelope_vertex_rs(&core, sector, rangeG, plot.radius, &dx, &dy);
        const lv_point_t next = {static_cast<lv_coord_t>(plot.radius + dx),
                                 static_cast<lv_coord_t>(plot.radius + dy)};
        changed = changed || next.x != plot.envelopePoints[sector].x ||
                  next.y != plot.envelopePoints[sector].y;
        plot.envelopePoints[sector] = next;
    }
    plot.envelopePoints[kImuSectorCount] = plot.envelopePoints[0];
    return changed;
}

}

bool build(lv_obj_t *cont, const GMeterMetrics::PlotBox &box, Plot &plot) {
    lv_obj_t *area = makeBareObject(cont, box.side, box.side);
    if (!area)
        return false;
    lv_obj_set_pos(area, box.x, box.y);
    plot.radius = static_cast<int16_t>(box.side / 2);
    plot.dotDx = kUnplacedPx;
    plot.dotDy = kUnplacedPx;
    if (!buildGuides(area, box.side, plot))
        return false;
    plot.envelope = makeEnvelope(area);
    plot.dot = makeDot(area);
    lv_obj_set_style_bg_color(plot.dot, lv_color_hex(ThemeManager::getEffectiveTextColor()),
                              LV_PART_MAIN);
    return plot.envelope != nullptr && plot.dot != nullptr;
}

void paintGuides(const Plot &plot, uint32_t rgb) {
    const lv_color_t color = lv_color_hex(rgb);
    lv_obj_set_style_border_color(plot.guides[OuterRing], color, LV_PART_MAIN);
    lv_obj_set_style_border_color(plot.guides[InnerRing], color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(plot.guides[HorizontalAxis], color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(plot.guides[VerticalAxis], color, LV_PART_MAIN);
}

void showDot(Plot &plot, const ImuCoreState &core, float rangeG) {
    int16_t dx = 0;
    int16_t dy = 0;
    imu_core_plot_rs(&core, rangeG, plot.radius, &dx, &dy);
    WidgetHelpers::setVisibleIfChanged(plot.dot, true);
    if (dx == plot.dotDx && dy == plot.dotDy)
        return;
    plot.dotDx = dx;
    plot.dotDy = dy;
    placeDot(plot);
}

void showEnvelope(Plot &plot, const ImuCoreState &core, float rangeG) {
    WidgetHelpers::setVisibleIfChanged(plot.envelope, true);
    if (!updateVertices(plot, core, rangeG))
        return;
    lv_line_set_points(plot.envelope, plot.envelopePoints, kImuSectorCount + 1);
}

void hideLive(const Plot &plot) {
    WidgetHelpers::setVisibleIfChanged(plot.dot, false);
    WidgetHelpers::setVisibleIfChanged(plot.envelope, false);
}

}
