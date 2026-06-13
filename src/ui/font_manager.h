#pragma once

#include <lvgl.h>

class FontManager {
  public:
    static void init();

    static void shutdown();

    static const lv_font_t *primary(uint8_t size);

    static const lv_font_t *secondary(uint8_t size);

    static const lv_font_t *label(uint8_t size);
};
