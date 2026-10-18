#pragma once

#include <stdint.h>

namespace GestureIntent {

struct Decision {
    bool cancelClick;
    bool fireSwipe;
    bool swipeLeft;
};

enum class TapTurn : uint8_t {
    None,
    Next,
    Previous,
};

inline constexpr int16_t kTapTravelLimitPx = 24;

inline constexpr int16_t kTapZoneCount = 3;

// The settings panel is scrollable, and LVGL drops gesture detection entirely
// while a scroll is live (lv_indev.c, indev_gesture: `if(scroll_obj) return`).
// So the close swipe is measured here rather than taken from LVGL.
inline constexpr int16_t kSettingsCloseTravelPx = 32;

[[nodiscard]] Decision decide(int16_t travelXSigned, int16_t travelY, bool startedOnClickable);

[[nodiscard]] bool closesSettings(int16_t travelYSigned, int16_t travelX);

[[nodiscard]] TapTurn tapTurn(int16_t releaseX, int16_t screenWidth, int16_t travel);

}
