#pragma once

#include "board.h"

#if defined(BOARD_CROWPANEL_28) || defined(BOARD_GENERIC_ILI9341)
    #include "boards/panel_ili9341_xpt2046.h"
#elif defined(BOARD_GENERIC_ILI9341_GT911)
    #include "boards/panel_ili9341_gt911.h"
#endif
