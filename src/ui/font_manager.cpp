// font_manager.cpp — Resolves font size to a compiled-in LVGL font pointer.

#include "font_manager.h"
#include "diag/logger.h"

void FontManager::init() {
    LOG_INFO("FONT", "FontManager ready (compile-time fonts: 12,14,16,20,24,32,48)");
}

const lv_font_t *FontManager::get(uint8_t size) {
    if (size >= 48) return &lv_font_montserrat_48;
#if LV_FONT_MONTSERRAT_40
    if (size >= 40) return &lv_font_montserrat_40;
#endif
#if LV_FONT_MONTSERRAT_36
    if (size >= 36) return &lv_font_montserrat_36;
#endif
    if (size >= 32) return &lv_font_montserrat_32;
    if (size >= 24) return &lv_font_montserrat_24;
    if (size >= 20) return &lv_font_montserrat_20;
    if (size >= 16) return &lv_font_montserrat_16;
    if (size >= 14) return &lv_font_montserrat_14;
    return &lv_font_montserrat_12;
}
