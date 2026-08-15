#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "config/config_types.h"

namespace PageManager {

void init();

void reloadFromStorage();

bool navigateTo(const char *pageId);

void navigateNext();

void navigatePrev();

const char *getDefaultPageId();

void updateWidgets();

[[nodiscard]] int16_t currentContentTopY();

void requestRebuild();

} // namespace PageManager
