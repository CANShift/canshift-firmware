#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace ValueCluster {

lv_obj_t *makeRow(lv_obj_t *cont);

lv_obj_t *makeValue(lv_obj_t *row, uint8_t devicePx);

lv_obj_t *makeUnit(lv_obj_t *row, const char *unit);

uint8_t rulePxFor(uint8_t devicePx);

lv_obj_t *makeRule(lv_obj_t *cont, uint8_t devicePx, uint32_t inkRgb);

}
