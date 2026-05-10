#pragma once
// psram.h — Runtime PSRAM detection.
//
// The CrowPanel 2.8" module ships with a WROOM (no PSRAM) and a WROVER
// variant (4 MB PSRAM). The build enables PSRAM unconditionally via
// `-DBOARD_HAS_PSRAM`; on WROOM modules the IDF init silently reports 0
// bytes and `isAvailable()` returns false. Detection runs once at boot.
//
// Sim env compiles to constant-false stubs — no ESP heap APIs available
// on host builds.

#include <stddef.h>

namespace canshift::hal::memory {

// Probe PSRAM once at boot and cache the result. Safe to call multiple
// times; subsequent calls are no-ops. Emits a single LOG_INFO("MEM", ...)
// describing the outcome.
void initPsram();

// True when usable PSRAM was detected at boot.
bool isPsramAvailable();

// Total PSRAM size in bytes (0 when none).
size_t getPsramSize();

// Largest currently-free PSRAM block in bytes (0 when none).
size_t getFreePsram();

} // namespace canshift::hal::memory
