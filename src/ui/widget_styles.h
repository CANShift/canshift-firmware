#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace WidgetStyles {

void applyContainerBase(lv_obj_t *cont, bool hasBorder, uint32_t borderRgb);

void applyContainerBaseNoBorder(lv_obj_t *cont);

void applyBarZone(lv_obj_t *zone, uint32_t bgRgb, lv_opa_t opa);

void applyBarFill(lv_obj_t *fill, uint32_t bgRgb);

void applyBarTrack(lv_obj_t *track);

void disableArcKnob(lv_obj_t *arc);

bool setTextColorIfChanged(lv_obj_t *label, uint32_t &cachedRgb, uint32_t targetRgb);
bool setBgColorIfChanged(lv_obj_t *obj, uint32_t &cachedRgb, uint32_t targetRgb);
bool setArcColorIfChanged(lv_obj_t *arc, uint32_t &cachedRgb, uint32_t targetRgb, lv_part_t part);

}
