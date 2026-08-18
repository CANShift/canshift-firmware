#pragma once

#include <stdint.h>

namespace GestureIntent {

struct Decision {
    bool cancelClick;
    bool fireSwipe;
    bool swipeLeft;
};

inline constexpr int16_t kTapTravelLimitPx = 24;

[[nodiscard]] Decision decide(int16_t travelXSigned, int16_t travelY, bool startedOnClickable);

} // namespace GestureIntent
