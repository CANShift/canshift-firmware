#pragma once
// screen_profile.h — Design→physical screen coordinate scaling (issues #17, #18).
//
// A dashboard's widget x/y/w/h are authored against a logical "design canvas"
// declared by `CfgDashboard.targetProfile`. The physical panel is described
// at compile time by `HW_DISPLAY_WIDTH` / `HW_DISPLAY_HEIGHT`. When the two
// differ (multi-screen-size landing, issue #18) widget coordinates must be
// scaled.
//
// v1 ships a single profile (`crowpanel-28` = 320×240) — design and physical
// dimensions match, so every scaled coordinate equals its input. The helpers
// are wired through the widget builders now so adding a second profile later
// is a catalog edit, not a code-path retrofit.
//
// Single source of truth for the catalog: `SCREEN_PROFILES` in
// canshift-core/src/schemas/screen-profile.ts. The firmware copy here must
// be kept in lockstep when new profiles land.

#include <stdint.h>

namespace ScreenProfile {

// Logical canvas dimensions a dashboard was authored against. Populated by
// `lookupDesignDimensions()` from the dashboard's `targetProfile` id.
struct DesignDimensions {
    uint16_t width;
    uint16_t height;
};

// ScaleFactors holds the multiplicative ratios design→physical. Computed
// once at boot in `initFromDashboard()`; widget builders read them through
// `scaleXVal()` / `scaleYVal()` (no hot-path float multiplication is added
// when the factors are 1.0).
struct ScaleFactors {
    float x;
    float y;
};

// Resolve the design canvas dimensions for a profile id (e.g. "crowpanel-28").
// Unknown / empty / null inputs fall back to the default profile, mirroring
// `resolveScreenProfile()` in canshift-core. The returned struct is by value;
// the catalog itself is internal to the .cpp.
DesignDimensions lookupDesignDimensions(const char *profileId);

// Compute and cache the boot-time scale factors from the active dashboard's
// `targetProfile`. Reads `ConfigLoader::getDashboardConfig().targetProfile`
// and divides physical dims (HW_DISPLAY_WIDTH/HEIGHT) by the design dims.
// Must run AFTER `ConfigLoader::loadAll()` and BEFORE any widget construction.
// Idempotent — safe to call again on `ConfigLoader::reloadAll()`.
void initFromDashboard();

// Active scale factors. Returns identity (1.0, 1.0) before `initFromDashboard`
// runs so any pre-boot caller (none today) gets a no-op rather than a
// divide-by-zero.
ScaleFactors getScaleFactors();

// Scale a design-space X coordinate or width to physical pixels. Integer math
// would suffice (precompute a Q8 ratio) once a non-identity scale ships; for
// v1 the multiplication runs at boot only via these inline helpers being
// called from widget construction, never inside the per-frame update path.
//
// TODO(#18): when the second profile lands, replace the float multiply with
// a precomputed Q8 fixed-point shift so the construction path stays free of
// libgcc soft-float on widget rebuild after a hot config reload.
int16_t scaleXVal(int16_t value);
int16_t scaleYVal(int16_t value);

} // namespace ScreenProfile
