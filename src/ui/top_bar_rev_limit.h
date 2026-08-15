#pragma once

#include <lvgl.h>

namespace TopBarRevLimit {

void build(lv_obj_t *bar);

void clearOverride();

[[nodiscard]] bool apply();

} // namespace TopBarRevLimit
