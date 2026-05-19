#pragma once
// icon_assets.h — Maps SensorIconName keys (from canshift-core) to LVGL
// asset paths on SPIFFS.
//
// Studio renders sensor icons as SVG components; the firmware loads matching
// .bin (RGB565) files from SPIFFS under "/assets/sensor_<name>.bin". The
// Unicode-glyph fallback path was removed in #681 — the Orbitron font we ship
// does not cover LVGL's private-use symbol range, so rendering LV_SYMBOL_*
// just produced empty squares. Widgets now skip the icon entirely when no
// .bin is present.

namespace IconAssets {

// LVGL FS path for the icon, "" if no mapping or the underlying .bin file is
// missing on the storage backend. Probes storage synchronously so callers can
// rely on "non-empty result == file exists and lv_img_set_src will succeed".
const char *path(const char *iconName);

// Probe an arbitrary LVGL FS path (e.g. user-supplied "S:/assets/custom.bin")
// and report whether the backing file exists on storage. Used by widgets that
// accept a free-form iconPath in addition to the iconName lookup.
bool exists(const char *lvglPath);

// Warm the LVGL image cache for an asset path so a later `lv_img_set_src`
// hits the cache instead of re-opening the SPIFFS file. Safe to call with a
// nullptr / empty string (no-op) and with a path that's already cached
// (no-op via LVGL's de-dupe). Must be called *after* `lv_init()` and the
// SPIFFS LVGL FS driver are up. Issue #956 — page rebuilds and theme
// toggles previously evicted the single-slot cache and forced reloads
// against a fragmented heap, leaving dashboard icons blank.
void preload(const char *lvglPath);

// Walk the loaded dashboard config and preload every unique sensor icon
// referenced by bar / warning / button widgets, plus the theme day/night
// icons used by the top bar. Intended to run once at boot, immediately
// after FontManager::init() when the heap still has a contiguous
// ~15 KB block available — the time window before page widgets allocate
// against the same pool.
void preloadDashboardAssets();

} // namespace IconAssets
