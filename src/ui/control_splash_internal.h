#pragma once

#include "control_state_rs.h"
#include "ui/control_splash_content.h"

#include <lvgl.h>
#include <stdint.h>

namespace ControlSplashInternal {

constexpr int16_t kPadPx = 8;
constexpr int16_t kRulePx = 2;
constexpr int16_t kKickerGapPx = 4;
constexpr int16_t kHeroGapPx = 13;
constexpr int16_t kBottomRulePx = 1;
constexpr int16_t kBottomGapPx = 7;
constexpr int16_t kCellGapPx = 20;
constexpr int16_t kCellKickerGapPx = 3;
constexpr int16_t kSegmentHeightPx = 11;
constexpr int16_t kSegmentGapPx = 3;

constexpr uint8_t kKickerFontPx = 10;
constexpr uint8_t kHeroFontPx = 64;
constexpr uint8_t kRefusedHeroFontPx = 48;
constexpr uint8_t kHeroUnitFontPx = 15;
constexpr uint8_t kCellValueFontPx = 17;

constexpr int16_t kKickerTrackingPx = 2;
constexpr int16_t kCellKickerTrackingPx = 2;

inline int16_t baselineDropPx(const lv_font_t *value, const lv_font_t *unit) {
    if (!value || !unit)
        return 0;
    const int16_t drop = static_cast<int16_t>(value->base_line - unit->base_line);
    return drop > 0 ? drop : 0;
}

struct Layer {
    lv_obj_t *root;
    lv_obj_t *rule;
    lv_obj_t *kicker;
    lv_obj_t *hero;
    lv_obj_t *heroUnit;
    lv_obj_t *sub;
    lv_obj_t *bottom;
    lv_obj_t *line;
    lv_obj_t *cells;
    lv_obj_t *segments;
    lv_obj_t *cellKicker[ControlSplashContent::kMaxCells];
    lv_obj_t *cellValue[ControlSplashContent::kMaxCells];
    lv_obj_t *cellUnit[ControlSplashContent::kMaxCells];
    lv_obj_t *segment[CONTROL_STEP_MAX];
};

bool build(Layer &layer);

} // namespace ControlSplashInternal
