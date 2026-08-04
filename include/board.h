#pragma once

#include "board_profile.h"
#include "config/board_profile_loader.h"

#if defined(BOARD_CROWPANEL_28)
    #include "boards/crowpanel_28.h"
#elif defined(BOARD_GENERIC_ILI9341)
    #include "boards/generic_ili9341.h"
#elif defined(BOARD_GENERIC_ILI9341_GT911)
    #include "boards/generic_ili9341_gt911.h"
#else
    #error                                                                                         \
        "No board profile selected. Define BOARD_CROWPANEL_28, BOARD_GENERIC_ILI9341, or BOARD_GENERIC_ILI9341_GT911 (or another supported BOARD_*) via platformio.ini build_flags."
#endif

inline constexpr const canshift::boards::BoardProfile &kBoard = canshift::boards::kActiveBoard;

namespace canshift {
namespace display {

inline constexpr uint16_t kWidth = boards::lcdLogicalWidth(boards::kActiveBoard.lcd);
inline constexpr uint16_t kHeight = boards::lcdLogicalHeight(boards::kActiveBoard.lcd);
inline constexpr uint8_t kColorDepth = boards::kActiveBoard.lcd.color_depth;

inline uint16_t width() {
    return boards::lcdLogicalWidth(boards::runtimeBoardProfile().lcd);
}
inline uint16_t height() {
    return boards::lcdLogicalHeight(boards::runtimeBoardProfile().lcd);
}

} // namespace display
} // namespace canshift
