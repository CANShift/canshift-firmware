#pragma once

#include <lvgl.h>
#include "config/config_types.h"

namespace ThemeManager {

void apply();

void init();

[[nodiscard]] bool isDayMode();

void toggleDayMode();

void setDayMode(bool day);

[[nodiscard]] CfgColor getEffectiveBgColor(const CfgColor &nightBg);

[[nodiscard]] uint32_t getEffectiveTextColor();

[[nodiscard]] uint32_t getEffectiveTextColor(uint32_t styleTextColor, bool respectDayMode);

} // namespace ThemeManager
