#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "config/config_types.h"

namespace PageManager {

void init();

bool navigateTo(const char *pageId);

void navigateNext();

void navigatePrev();

const char *getDefaultPageId();

void updateWidgets();

void requestRebuild();

} // namespace PageManager
