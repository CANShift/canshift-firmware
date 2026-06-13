#pragma once

#include <lvgl.h>

namespace GestureController {

using SwipeHandler = void (*)(lv_dir_t direction);

void setSwipeHandler(SwipeHandler handler);

using VerticalSwipeHandler = void (*)(lv_dir_t direction);

void setVerticalSwipeHandler(VerticalSwipeHandler handler);

void checkGestures();

} // namespace GestureController
