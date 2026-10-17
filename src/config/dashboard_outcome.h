#pragma once

#include <stdint.h>

namespace ConfigLoader {

enum class DashboardOutcome : uint8_t {
    Loaded = 0,
    RestoredPrevious,
    Refused,
    Unreadable,
    NoMemory,
};

inline constexpr uint8_t kDashboardOutcomeCount = 5;

} // namespace ConfigLoader
