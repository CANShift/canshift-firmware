#pragma once

#include <cstdint>

namespace SimDisplay {

bool init(int width, int height, int zoom);

void pumpEvents();

bool quitRequested();

bool screenshotRequested();

int pollKey();

void writeScreenshot(const char *path);

} // namespace SimDisplay
