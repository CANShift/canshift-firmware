#include "ui/gesture_intent.h"

#include "app_config.h"

namespace {

constexpr int16_t kSwipeThroughTravelPx = 32;
constexpr int16_t kSwipeThroughAxisRatio = 2;

int16_t magnitude(int16_t value) {
    return value < 0 ? static_cast<int16_t>(-value) : value;
}

} // namespace

namespace GestureIntent {

Decision decide(int16_t travelXSigned, int16_t travelY, bool startedOnClickable) {
    const int16_t travelX = magnitude(travelXSigned);
    const int16_t travel = travelX > travelY ? travelX : travelY;
    const bool swipeLeft = travelXSigned < 0;

    if (travelX < SWIPE_CANCEL_THRESHOLD_PX)
        return {travel >= kTapTravelLimitPx, false, swipeLeft};

    if (!startedOnClickable)
        return {true, true, swipeLeft};

    const bool horizontalDominates =
        travelX >= kSwipeThroughTravelPx && travelX > kSwipeThroughAxisRatio * travelY;
    return {travel >= kTapTravelLimitPx || horizontalDominates, horizontalDominates, swipeLeft};
}

bool closesSettings(int16_t travelYSigned, int16_t travelX) {
    if (travelYSigned > -kSettingsCloseTravelPx)
        return false;
    const int16_t travelY = magnitude(travelYSigned);
    return travelY > kSwipeThroughAxisRatio * magnitude(travelX);
}

} // namespace GestureIntent
