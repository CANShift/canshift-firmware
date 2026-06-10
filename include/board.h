#pragma once
// Each boards/<board>.h defines kActiveBoard + the LGFX class.
#include "board_profile.h"

#if defined(BOARD_CROWPANEL_28)
    #include "boards/crowpanel_28.h"
#else
    #error                                                                                         \
        "No board profile selected. Define BOARD_CROWPANEL_28 (or another supported BOARD_*) via platformio.ini build_flags."
#endif

inline constexpr const canshift::boards::BoardProfile &kBoard = canshift::boards::kActiveBoard;
