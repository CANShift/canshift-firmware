// font_manager.cpp — Resolves font size to a compiled-in LVGL font pointer.
//
// Fonts compiled into flash are the kerning-stripped variants we ship under
// src/ui/fonts/lv_font_montserrat_<N>_nk.c (regenerated via
// scripts/regen_montserrat_no_kern.py). Upstream LVGL Montserrat sizes are
// disabled in include/lv_conf.h — see issue #407.

#include "font_manager.h"
#include "diag/logger.h"

void FontManager::init() {
    LOG_INFO("FONT", "FontManager ready (compile-time fonts: 12,14,16,20,24,32,48 — no-kern)");
}

const lv_font_t *FontManager::get(uint8_t size) {
    if (size >= 48) return &lv_font_montserrat_48_nk;
    if (size >= 32) return &lv_font_montserrat_32_nk;
    if (size >= 24) return &lv_font_montserrat_24_nk;
    if (size >= 20) return &lv_font_montserrat_20_nk;
    if (size >= 16) return &lv_font_montserrat_16_nk;
    if (size >= 14) return &lv_font_montserrat_14_nk;
    return &lv_font_montserrat_12_nk;
}
