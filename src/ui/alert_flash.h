#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace AlertFlash {

constexpr uint8_t MAX_TRACKED_LABELS = 4;

struct State {
    lv_obj_t *overlay = nullptr;
    bool active = false;
    lv_obj_t *labels[MAX_TRACKED_LABELS] = {};
    uint32_t restoreRgb[MAX_TRACKED_LABELS] = {};
    uint8_t labelCount = 0;
};

void attach(State &s, lv_obj_t *container);

void watchLabel(State &s, lv_obj_t *label, uint32_t restoreRgb);

void update(State &s, float value, float threshold);

} // namespace AlertFlash
