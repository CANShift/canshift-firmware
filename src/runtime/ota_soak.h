#pragma once

#include <stdint.h>

namespace OtaSoak {

enum class Moment : uint8_t {
    DashFrame = 0,
    WorkshopFrame,
    LeavingWorkshop,
};

inline constexpr uint32_t kSoakMs = 60000;

[[nodiscard]] inline bool marksValid(Moment moment, uint32_t uptimeMs) {
    if (moment == Moment::WorkshopFrame)
        return false;
    return uptimeMs >= kSoakMs;
}

}
