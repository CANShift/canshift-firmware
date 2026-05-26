#pragma once
// font_manager.h — Intent-based font lookup for the active dashboard family.
//
// Three intents map to three weights (issues #431 + #487):
//   primary   → Black  / 900 weight — RPM, speed, gear, lap time   (32, 48)
//   secondary → Bold   / 700 weight — boost, oil temp, voltage     (20, 24)
//   label     → Medium / 500 weight — small labels, top bar, hints (12, 14, 16)
//
// Each call snaps the requested size DOWN to the nearest cached size within
// the intent's tier. Picking the nearest cached size up front lets the widget
// code stay unchanged when we add/drop a size — the ramp never overflows.
//
// Fonts are loaded at boot from SPIFFS (`S:/fonts/<family>_<weight>_<N>.bin`)
// via `lv_font_load()` and cached in a static array. If a size fails to load,
// the accessor falls back to the built-in `lv_font_orbitron_medium_14_nk`
// shipped in flash, so text always renders even without `pio run -t uploadfs`.
//
// Family selection (issues #971 + #500): `init(family)` resolves the
// `CfgDashboard.fontFamily` id to a `FontFamilyAssets` catalog row. v1 ships
// a single family (`orbitron`) so the lookup is trivial; the indirection is
// the scaffold every future family slots into. Unknown / empty ids log a
// WARN and fall back to the canonical default so a hand-edited dashboard.json
// never bricks rendering. Mirrors `resolveFontFamily()` in canshift-core.
//
// SPIFFS must be mounted (StorageDriver::init) and the LVGL FS driver
// registered (LvglFsDriver::init) before calling FontManager::init().

#include <lvgl.h>

class FontManager {
  public:
    // Loads font assets for the active dashboard family. Unknown / empty /
    // null ids log a WARN and fall back to the canonical default (`orbitron`)
    // so the device always renders text. Safe to call multiple times —
    // re-loading is a no-op.
    static void init(const char *family);

    // Loads font assets for the canonical default family (`orbitron`).
    // Kept so test harnesses and pre-#971 call sites compile unchanged.
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
