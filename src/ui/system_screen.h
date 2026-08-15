#pragma once

#include "ui/severity.h"

#include <lvgl.h>
#include <stdint.h>

namespace SystemScreen {

struct FootSpec {
    lv_flex_flow_t flow;
    int16_t gapPx;
};

struct Frame {
    lv_obj_t *screen;
    Severity::Surface header;
    lv_obj_t *foot;
};

[[nodiscard]] Frame build(const Severity::Spec &spec, const FootSpec &foot);

lv_obj_t *addMonoLine(lv_obj_t *parent, const char *text, uint32_t rgb, uint8_t devicePx,
                      uint16_t lineHeightPermille);

lv_obj_t *addKicker(lv_obj_t *parent, const char *text, uint32_t rgb);

void setTopGap(lv_obj_t *obj, int16_t gapPx);

void present(const Frame &frame);

} // namespace SystemScreen
