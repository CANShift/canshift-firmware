#include "lvgl_pool_alloc.h"

#include "diag/logger.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {

constexpr uint32_t kFatalRelogMs = 5000;

// Returning NULL is not an option: lv_tlsf_create() only rejects a misaligned
// pointer, and NULL is aligned, so it would run control_constructor() straight
// through address 0. Halting keeps the reason on the wire instead of trading it
// for a reset loop.
[[noreturn]] void haltUnpooled(size_t size) {
    for (;;) {
        LOG_ERROR("LVGL",
                  "no pool: %u B unavailable from PSRAM and internal RAM. The UI cannot start. "
                  "This build asserts BOARD_HAS_PSRAM — check the module actually carries it.",
                  static_cast<unsigned>(size));
        delay(kFatalRelogMs);
    }
}

} // namespace

extern "C" void *canshift_lvgl_pool_alloc(size_t size) {
    void *pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (pool != nullptr)
        return pool;

    pool = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pool != nullptr) {
        LOG_WARN("LVGL", "pool of %u B came from internal RAM — PSRAM refused it",
                 static_cast<unsigned>(size));
        return pool;
    }

    haltUnpooled(size);
}
