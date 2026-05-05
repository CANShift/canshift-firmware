#pragma once
// font_manager.h — Resolves a numeric size to a compiled-in LVGL font.
// Fonts live in flash (LV_FONT_MONTSERRAT_* in lv_conf.h), not the
// LVGL heap, so they cost zero RAM. Sizes outside the supported set
// snap to the nearest available one.

#include <lvgl.h>

class FontManager {
public:
    // Kept for boot_sequence compatibility. No-op (fonts are link-time).
    static void init();
    static const lv_font_t *get(uint8_t size);
};
