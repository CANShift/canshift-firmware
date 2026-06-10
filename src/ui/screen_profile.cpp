#include "screen_profile.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "hardware_profile.h"

#include <string.h>

namespace ScreenProfile {

namespace {

struct ProfileEntry {
    const char *id;
    uint16_t designWidth;
    uint16_t designHeight;
};

// Mirrors DEFAULT_SCREEN_PROFILE_ID in canshift-core.
constexpr ProfileEntry kProfiles[] = {
    {"crowpanel-28", 320, 240},
};
constexpr const char *kDefaultProfileId = "crowpanel-28";

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
    // Non-recursive — a typo in kDefaultProfileId surfaces at boot.
    for (const ProfileEntry &entry : kProfiles) {
        if (strcmp(entry.id, kDefaultProfileId) == 0) {
            return entry;
        }
    }
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

    if (design.width == 0 || design.height == 0) {
        LOG_ERROR("SCRN", "profile '%s' has zero design dims — scale forced to identity",
                  dash.targetProfile);
        s_factors = {1.0f, 1.0f};
        return;
    }

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
