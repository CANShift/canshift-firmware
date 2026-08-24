#include "lvgl_pool_alloc.h"

#include "diag/logger.h"

#include <Arduino.h>
#include <esp_system.h>

#ifndef SIM_BUILD
    #include <esp_heap_caps.h>
#else
    #include <stdlib.h>
#endif

namespace {

// The real trigger is a module whose PSRAM is absent or refuses the pool, which
// no board on the bench does. Build with -DLVGL_POOL_FORCE_FAIL=<n> to make the
// first n tiers refuse: 1 exercises the internal-RAM fallback, 2 the halt. The
// simulator routes through this same function, so both are reachable without
// hardware. See docs/reference/build-flags.md.
#ifndef LVGL_POOL_FORCE_FAIL
    #define LVGL_POOL_FORCE_FAIL 0
#endif

enum class Tier : uint8_t { Psram = 0, Internal = 1 };

void *allocTier(size_t size, Tier tier) {
    if (static_cast<uint8_t>(tier) < LVGL_POOL_FORCE_FAIL)
        return nullptr;
#ifdef SIM_BUILD
    return tier == Tier::Psram ? malloc(size) : nullptr;
#else
    const uint32_t caps =
        tier == Tier::Psram ? MALLOC_CAP_SPIRAM : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return heap_caps_malloc(size, caps);
#endif
}

// Returning NULL is not an option: lv_tlsf_create() only rejects a misaligned
// pointer, and NULL is aligned, so it would run control_constructor() straight
// through address 0.
//
// Restarting rather than halting is what arms the recovery this project already
// has: a freshly flashed image stays PENDING_VERIFY until the UI loop marks it
// valid, so an image that dies here gets rolled back to the last known-good one.
// A halt never reboots, so the rollback never fires and the board stays on the
// bad image. Same shape as canshift_lvgl_assert_handler.
[[noreturn]] void restartUnpooled(size_t size) {
    LOG_ERROR("LVGL",
              "no pool: %u B unavailable from PSRAM and internal RAM — restarting. This build "
              "asserts BOARD_HAS_PSRAM; check the module actually carries it.",
              static_cast<unsigned>(size));
    delay(50);
    esp_restart();
    for (;;) {}
}

} // namespace

extern "C" void *canshift_lvgl_pool_alloc(size_t size) {
    void *pool = allocTier(size, Tier::Psram);
    if (pool != nullptr)
        return pool;

    pool = allocTier(size, Tier::Internal);
    if (pool != nullptr) {
        LOG_WARN("LVGL", "pool of %u B came from internal RAM — PSRAM refused it",
                 static_cast<unsigned>(size));
        return pool;
    }

    restartUnpooled(size);
}
