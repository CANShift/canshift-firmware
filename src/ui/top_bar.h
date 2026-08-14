#pragma once

#include <cstdint>
#include "config/config_types.h"

#include <lvgl.h>

namespace TopBar {

void init();

void rebuild();

void update();

int16_t getHeight();

void reapplyTheme();

void applyPage(const CfgPage &page);

} // namespace TopBar
