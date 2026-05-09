#pragma once
// font_manager.h — Intent-based font lookup for the Orbitron typography tier.
//
// Three intents map to three weights (issues #431 + #487):
//   primary   → Orbitron Black  (900) — RPM, speed, gear, lap time   (32)
//   secondary → Orbitron Bold   (700) — boost, oil temp, voltage     (20, 24)
//   label     → Orbitron Medium (500) — small labels, top bar, hints (12, 14, 16)
//
// Each call snaps the requested size DOWN to the nearest cached size within
// the intent's tier. Picking the nearest cached size up front lets the widget
// code stay unchanged when we add/drop a size — the ramp never overflows.
//
// Fonts are loaded at boot from SPIFFS (`S:/fonts/orbitron_<weight>_<N>.bin`)
// via `lv_font_load()` and cached in a static array. If a size fails to load,
// the accessor falls back to the built-in `lv_font_orbitron_medium_14_nk`
// shipped in flash, so text always renders even without `pio run -t uploadfs`.
//
// SPIFFS must be mounted (StorageDriver::init) and the LVGL FS driver
// registered (LvglFsDriver::init) before calling FontManager::init().

#include <lvgl.h>

class FontManager {
public:
    // Loads all Orbitron .bin fonts from SPIFFS and caches them.
    // Safe to call multiple times — re-loading is a no-op.
    static void init();

    // Frees every loaded font and clears the cache. Optional teardown hook.
    static void shutdown();

    // Primary values — Orbitron Black 900. Snaps to 32 px.
    static const lv_font_t *primary(uint8_t size);

    // Secondary values — Orbitron Bold 700. Snaps to 20 or 24 px.
    static const lv_font_t *secondary(uint8_t size);

    // Labels & body — Orbitron Medium 500. Snaps to 12, 14, or 16 px.
    static const lv_font_t *label(uint8_t size);
};
