#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace SettingsDrag {

void update(lv_indev_t *indev, lv_indev_state_t state);

[[nodiscard]] bool inHotzone(int16_t y);

}
