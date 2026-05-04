#pragma once
// error_bar.h — Bottom overlay bar showing active firmware errors.
//
// Lives on lv_layer_top() so it persists across page changes.
// Collapsed (20px): latest error code + short message + dismiss button.
// Expanded (up to 80px): list of up to 3 active errors, each dismissible.
// Hidden entirely when no errors are active.
//
// Call init() from PageManager::init() after creating other overlays.
// Call update() from PageManager::updateWidgets() every UI tick.

#include <lvgl.h>

namespace ErrorBar {

void init();
void update();

} // namespace ErrorBar
