#pragma once

#include <cstdint>
#include <lvgl.h>

namespace TopBar {

void init();

void update();

int16_t getHeight();

void setTopInset(int16_t px);

void reapplyTheme();

} // namespace TopBar
