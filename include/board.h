#pragma once

#include "board_profile.h"
#include "config/board_profile_loader.h"

#include "boards/catalog.h"

namespace canshift::boards {

#if defined(BOARD_CROWPANEL_28)
inline constexpr const BoardProfile &kActiveBoard = kCrowpanel28;
#elif defined(BOARD_GENERIC_ILI9341)
inline constexpr const BoardProfile &kActiveBoard = kGenericIli9341;
#elif defined(BOARD_GENERIC_ILI9341_GT911)
inline constexpr const BoardProfile &kActiveBoard = kGenericIli9341Gt911;
#elif defined(BOARD_GENERIC_ESP32S3)
inline constexpr const BoardProfile &kActiveBoard = kGenericEsp32s3;
#elif defined(BOARD_WAVESHARE_S3_28)
inline constexpr const BoardProfile &kActiveBoard = kWaveshareS328;
#else
    #error                                                                                         \
        "No default board selected. Define BOARD_CROWPANEL_28, BOARD_GENERIC_ILI9341, BOARD_GENERIC_ILI9341_GT911, BOARD_GENERIC_ESP32S3, or BOARD_WAVESHARE_S3_28 (or another supported BOARD_*) via platformio.ini build_flags."
#endif

} // namespace canshift::boards

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
