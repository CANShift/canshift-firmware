#include "top_bar_separator_link.h"

namespace TopBarSeparatorLink {

int8_t linkedFlagForSeparator(const Tracker &tracker) {
    return tracker.lastModeFlagIdx;
}

int8_t registerModeFlag(Tracker &tracker, int8_t registeredIdx, int8_t dynCount) {
    tracker.lastModeFlagIdx = registeredIdx;
    const int8_t pending = tracker.pendingSepNeedingNext;
    tracker.pendingSepNeedingNext = NO_FLAG;
    if (pending >= 0 && pending < dynCount)
        return pending;
    return NO_FLAG;
}

void registerSeparator(Tracker &tracker, int8_t registeredIdx) {
    tracker.pendingSepNeedingNext = registeredIdx;
}

void resetTracking(Tracker &tracker) {
    tracker.lastModeFlagIdx = NO_FLAG;
    tracker.pendingSepNeedingNext = NO_FLAG;
}

bool hasValidLink(int8_t linkedFlagIdx, int8_t dynCount) {
    return linkedFlagIdx >= 0 && linkedFlagIdx < dynCount;
}

bool wantsHidden(int8_t nextFlagIdx, int8_t dynCount, bool linkedFlagHidden, bool nextFlagHidden) {
    const bool nextHidden = nextFlagIdx < 0 || nextFlagIdx >= dynCount || nextFlagHidden;
    return linkedFlagHidden || nextHidden;
}

} // namespace TopBarSeparatorLink
