#pragma once
// board.h — Selects the active board profile from the BOARD_* build flag.
//
// Each supported board ships an include/boards/<board>.h that:
//   - defines `constexpr canshift::boards::BoardProfile kActiveBoard`
//   - declares the `LGFX` class for that board's panel + touch + backlight.
//
// Add a new board by adding an `#elif defined(BOARD_<YOUR_BOARD>)` clause
// below and a matching env in platformio.ini.

#include "board_profile.h"

#if defined(BOARD_CROWPANEL_28)
    #include "boards/crowpanel_28.h"
#else
    #error "No board profile selected. Define BOARD_CROWPANEL_28 (or another supported BOARD_*) via platformio.ini build_flags."
#endif

inline constexpr const canshift::boards::BoardProfile& kBoard = canshift::boards::kActiveBoard;
