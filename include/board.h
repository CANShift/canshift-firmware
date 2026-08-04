#pragma once

#include "board_profile.h"

#if defined(BOARD_CROWPANEL_28)
    #include "boards/crowpanel_28.h"
#elif defined(BOARD_GENERIC_ILI9341)
    #include "boards/generic_ili9341.h"
#else
    #error                                                                                         \
        "No board profile selected. Define BOARD_CROWPANEL_28 or BOARD_GENERIC_ILI9341 (or another supported BOARD_*) via platformio.ini build_flags."
#endif

inline constexpr const canshift::boards::BoardProfile &kBoard = canshift::boards::kActiveBoard;

namespace canshift {
namespace display {

inline constexpr uint16_t kWidth = boards::lcdLogicalWidth(boards::kActiveBoard.lcd);
inline constexpr uint16_t kHeight = boards::lcdLogicalHeight(boards::kActiveBoard.lcd);
inline constexpr uint8_t kColorDepth = boards::kActiveBoard.lcd.color_depth;

} // namespace display
} // namespace canshift
