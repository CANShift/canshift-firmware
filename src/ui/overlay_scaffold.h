#pragma once

#include "theme_tokens.h"

#include <lvgl.h>
#include <stdint.h>

namespace OverlayScaffold {

inline constexpr uint32_t kBackdropRgb = ThemeTokens::kGroundNight;

lv_obj_t *createRoot(uint32_t backdropRgb = kBackdropRgb);

lv_obj_t *createCenterColumn(lv_obj_t *root, int16_t rowGapPx);

void startBreath(lv_obj_t *obj);

void stopBreath(lv_obj_t *obj);

}
