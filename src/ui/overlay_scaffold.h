#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace OverlayScaffold {

inline constexpr uint32_t kBackdropRgb = 0x0D0D0D;
inline constexpr uint16_t kSpinnerSpanDeg = 80;
inline constexpr uint32_t kSpinnerTickMs = 16;

lv_obj_t *createRoot(uint32_t backdropRgb = kBackdropRgb);

lv_obj_t *createCenterColumn(lv_obj_t *root, int16_t rowGapPx);

lv_obj_t *makeUsbIcon(lv_obj_t *parent, uint32_t rgb);

lv_obj_t *makeSpinnerArc(lv_obj_t *parent, int16_t diameterPx);

void stepSpinner(lv_obj_t *arc, uint16_t &angleDeg);

void startBreath(lv_obj_t *obj);

void stopBreath(lv_obj_t *obj);

} // namespace OverlayScaffold
