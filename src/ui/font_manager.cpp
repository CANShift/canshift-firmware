// font_manager.cpp — Runtime font loader from SPIFFS

#include "font_manager.h"
#include "diag/logger.h"
#include <stdio.h>

constexpr uint8_t  FontManager::SIZES[];
lv_font_t         *FontManager::s_fonts[FontManager::N_FONTS] = {};

void FontManager::init() {
    for (uint8_t i = 0; i < N_FONTS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "S:/fonts/montserrat_%d.bin", SIZES[i]);
        s_fonts[i] = lv_font_load(path);
        if (s_fonts[i]) {
            LOG_INFO("FONT", "Loaded montserrat_%d from SPIFFS", SIZES[i]);
        } else {
            LOG_WARN("FONT", "montserrat_%d.bin not found — using fallback", SIZES[i]);
        }
    }
}

const lv_font_t *FontManager::get(uint8_t size) {
    for (uint8_t i = 0; i < N_FONTS; i++) {
        if (SIZES[i] == size && s_fonts[i]) return s_fonts[i];
    }
    return LV_FONT_DEFAULT;
}
