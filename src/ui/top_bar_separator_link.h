#pragma once

#include <stdint.h>

namespace TopBarSeparatorLink {

constexpr int8_t NO_FLAG = -1;

struct Tracker {
    int8_t lastModeFlagIdx = NO_FLAG;
    int8_t pendingSepNeedingNext = NO_FLAG;
};

int8_t linkedFlagForSeparator(const Tracker &tracker);

int8_t registerModeFlag(Tracker &tracker, int8_t registeredIdx, int8_t dynCount);

void registerSeparator(Tracker &tracker, int8_t registeredIdx);

void resetTracking(Tracker &tracker);

bool hasValidLink(int8_t linkedFlagIdx, int8_t dynCount);

bool wantsHidden(int8_t nextFlagIdx, int8_t dynCount, bool linkedFlagHidden, bool nextFlagHidden);

} // namespace TopBarSeparatorLink
