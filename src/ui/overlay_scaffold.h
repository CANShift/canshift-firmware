#pragma once

#include "theme_tokens.h"

#include <lvgl.h>
#include <stdint.h>

namespace OverlayScaffold {

inline constexpr uint32_t kBackdropRgb = ThemeTokens::kGroundNight;

lv_obj_t *createRoot(uint32_t backdropRgb = kBackdropRgb);

lv_obj_t *createCenterColumn(lv_obj_t *root, int16_t rowGapPx);

lv_obj_t *makeUsbIcon(lv_obj_t *parent, uint32_t rgb);

void startBreath(lv_obj_t *obj);

void stopBreath(lv_obj_t *obj);

} // namespace OverlayScaffold
