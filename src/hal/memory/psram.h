#pragma once

#include <stddef.h>

namespace canshift::hal::memory {

void initPsram();

bool isPsramAvailable();

size_t getPsramSize();

size_t getFreePsram();

} // namespace canshift::hal::memory
