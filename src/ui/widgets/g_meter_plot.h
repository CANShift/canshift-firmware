#pragma once

#include "imu_core_rs.h"
#include "ui/widgets/g_meter_metrics.h"

#include <lvgl.h>
#include <stdint.h>

namespace GMeterPlot {

struct Plot {
    lv_obj_t *guides[4];
    lv_obj_t *envelope;
    lv_obj_t *dot;
    lv_point_t envelopePoints[kImuSectorCount + 1];
    int16_t radius;
    int16_t dotDx;
    int16_t dotDy;
};

[[nodiscard]] bool build(lv_obj_t *cont, const GMeterMetrics::PlotBox &box, Plot &plot);
void paintGuides(const Plot &plot, uint32_t rgb);
void showDot(Plot &plot, const ImuCoreState &core, float rangeG);
void showEnvelope(Plot &plot, const ImuCoreState &core, float rangeG);
void hideLive(const Plot &plot);

}
