#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "config/config_types.h"
#include "config/dashboard_outcome.h"

namespace PageManager {

void init();

[[nodiscard]] ConfigLoader::DashboardOutcome reloadFromStorage();

bool navigateTo(const char *pageId);

void navigateNext();

void navigatePrev();

const char *getDefaultPageId();

void updateWidgets();

void publishPages();

[[nodiscard]] int16_t currentContentTopY();

void requestRebuild();

void releaseAll();

}
