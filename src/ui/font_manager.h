#pragma once
// font_manager.h — Resolves a numeric size to a Montserrat font.
//
// Fonts are loaded at boot from SPIFFS (`S:/fonts/montserrat_<N>.bin`) via
// `lv_font_load()` and cached in a static array. If a size fails to load,
// `get()` falls back to the built-in `lv_font_montserrat_14_nk` shipped in
// flash, so text always renders even without `pio run -t uploadfs`.
//
// SPIFFS must be mounted (StorageDriver::init) and the LVGL FS driver
// registered (LvglFsDriver::init) before calling FontManager::init().

#include <lvgl.h>

class FontManager {
public:
    // Loads all Montserrat .bin fonts from SPIFFS and caches them.
    // Safe to call multiple times — re-loading is a no-op.
    static void init();

    // Frees every loaded font and clears the cache. Optional teardown hook.
    static void shutdown();

    // Snaps `size` down to the nearest cached size and returns the font.
    // Falls back to the built-in 14px font if the cached entry is null.
    static const lv_font_t *get(uint8_t size);
};
