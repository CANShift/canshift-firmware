#pragma once

#include <cstdint>
#include "config/config_types.h"

#include <lvgl.h>

namespace TopBar {

void init();

void update();

int16_t getHeight();

void setTopInset(int16_t px);

void reapplyTheme();

void applyPage(const CfgPage &page);

} // namespace TopBar
