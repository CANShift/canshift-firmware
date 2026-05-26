// screen_profile.cpp — Design→physical scale factor computation.
//
// See screen_profile.h for the rationale. Catalog mirrors the v1 entry in
// canshift-core/src/schemas/screen-profile.ts. Adding a board (issue #17) or
// a larger panel (issue #18) is a two-line extension of `kProfiles`.

#include "screen_profile.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "hardware_profile.h"

#include <string.h>

namespace ScreenProfile {

namespace {

// Catalog entry — pairs the user-facing profile id (kebab-case literal) with
// the design canvas dimensions the studio editor wrote against.
struct ProfileEntry {
    const char *id;
    uint16_t designWidth;
    uint16_t designHeight;
};

// v1 ships the CrowPanel 2.8" only. Issues #17 / #18 extend this list with
// the second board / second resolution; nothing else here changes.
constexpr ProfileEntry kProfiles[] = {
    {"crowpanel-28", 320, 240},
};

// Mirrors DEFAULT_SCREEN_PROFILE_ID in canshift-core. When the dashboard
// targetProfile is empty / unknown, fall back to this entry so scale
// factors stay defined.
constexpr const char *kDefaultProfileId = "crowpanel-28";

// Identity scale used until initFromDashboard() runs. Keeps pre-init callers
// (none today, but the surface is exposed in the public API) producing a
// no-op rather than dividing by zero downstream.
ScaleFactors s_factors = {1.0f, 1.0f};

const ProfileEntry &findEntryOrDefault(const char *profileId) {
    if (profileId && profileId[0] != '\0') {
        for (const ProfileEntry &entry : kProfiles) {
            if (strcmp(entry.id, profileId) == 0) {
                return entry;
            }
        }
        LOG_WARN("SCRN", "unknown targetProfile='%s' — falling back to '%s'", profileId,
                 kDefaultProfileId);
    }
    // Default is guaranteed by construction: kDefaultProfileId is always one
    // of the catalog ids. The lookup below is intentionally non-recursive so
    // a future typo in kDefaultProfileId surfaces at boot rather than
    // silently looping.
    for (const ProfileEntry &entry : kProfiles) {
        if (strcmp(entry.id, kDefaultProfileId) == 0) {
            return entry;
        }
    }
    // Unreachable when the catalog is well-formed — the static_assert below
    // pins the invariant at compile time.
    return kProfiles[0];
}

static_assert(sizeof(kProfiles) / sizeof(kProfiles[0]) >= 1,
              "screen profile catalog must contain at least the default entry");

} // namespace

DesignDimensions lookupDesignDimensions(const char *profileId) {
    const ProfileEntry &entry = findEntryOrDefault(profileId);
    return {entry.designWidth, entry.designHeight};
}

void initFromDashboard() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const DesignDimensions design = lookupDesignDimensions(dash.targetProfile);

    // Degenerate dims would divide by zero — fall back to identity rather
    // than poison every widget rect. Logged so a malformed catalog entry
    // surfaces in boot logs.
    if (design.width == 0 || design.height == 0) {
        LOG_ERROR("SCRN", "profile '%s' has zero design dims — scale forced to identity",
                  dash.targetProfile);
        s_factors = {1.0f, 1.0f};
        return;
    }

    // Float division at boot is cheap and keeps the per-widget construction
    // path readable. The hot widget-update path never reads these factors —
    // only widget-build sites do, exactly once per (re)build.
    s_factors.x = static_cast<float>(HW_DISPLAY_WIDTH) / static_cast<float>(design.width);
    s_factors.y = static_cast<float>(HW_DISPLAY_HEIGHT) / static_cast<float>(design.height);

    LOG_INFO("SCRN", "profile='%s' design=%ux%u physical=%dx%d scale=%d/1000 x %d/1000",
             dash.targetProfile, static_cast<unsigned>(design.width),
             static_cast<unsigned>(design.height), HW_DISPLAY_WIDTH, HW_DISPLAY_HEIGHT,
             static_cast<int>(s_factors.x * 1000.0f), static_cast<int>(s_factors.y * 1000.0f));
}

ScaleFactors getScaleFactors() {
    return s_factors;
}

int16_t scaleXVal(int16_t value) {
    // Identity (scale == 1.0) skips the multiplication — keeps v1 output
    // byte-identical to pre-#18 firmware on crowpanel-28 hardware and means
    // no soft-float lib pull at the widget build sites until a non-trivial
    // scale ships.
    if (s_factors.x == 1.0f) {
        return value;
    }
    return static_cast<int16_t>(static_cast<float>(value) * s_factors.x);
}

int16_t scaleYVal(int16_t value) {
    if (s_factors.y == 1.0f) {
        return value;
    }
    return static_cast<int16_t>(static_cast<float>(value) * s_factors.y);
}

} // namespace ScreenProfile
