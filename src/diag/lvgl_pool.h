#pragma once

#include <stdint.h>

namespace LvglPool {

// The largest deferred surface measured at 21.7 KB (ControlSplash, #259). A
// build attempt that overruns the pool trips LV_USE_ASSERT_MALLOC, which
// restarts the board — so a surface asks before it allocates.
constexpr uint32_t kDeferredSurfaceBytes = 24U * 1024U;

void report(const char *phase);

[[nodiscard]] bool hasHeadroomFor(uint32_t bytes, const char *surface);

} // namespace LvglPool
