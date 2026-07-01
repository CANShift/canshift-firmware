#pragma once

#include <lvgl.h>

namespace TouchDriver {

void init();

void readCallback(lv_indev_drv_t *drv, lv_indev_data_t *data);

bool isCalibrated();

void calibrate();

void resetCalibration();

} // namespace TouchDriver
