#pragma once

#include <lvgl.h>

class FontManager {
  public:
    static void init();

    static void shutdown();

    static const lv_font_t *value(uint8_t devicePx);

    static const lv_font_t *units();

    static const lv_font_t *label(uint8_t size);
};
