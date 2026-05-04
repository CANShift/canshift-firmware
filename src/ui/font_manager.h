#pragma once
// font_manager.h — Runtime font loader from SPIFFS
// Loads Montserrat .bin files at boot via lv_font_load(), caches them.
// Falls back to the built-in lv_font_montserrat_14 if a file is missing.

#include <lvgl.h>

class FontManager {
public:
    static void init();
    static const lv_font_t *get(uint8_t size);

private:
    static constexpr uint8_t SIZES[]  = {12, 14, 16, 20, 24, 32, 48};
    static constexpr uint8_t N_FONTS  = 7;
    static lv_font_t        *s_fonts[N_FONTS];
};
