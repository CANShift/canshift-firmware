#pragma once
// icon_assets.h — Maps SensorIconName keys (from canshift-core) to LVGL
// asset sources. Two backends:
//
//  - Baked-into-flash dsc pointers (icon_assets_baked.cpp) for the icons
//    used by the bundled dashboard + theme glyphs. Resolved by
//    `resolveSource()` first so the cache/heap/FS path is bypassed entirely
//    — fixes the page-rebuild blank-icon regression (issue #1261).
//
//  - SPIFFS `.bin` files under `/assets/sensor_<name>.bin` for the rest.
//    Returned as an LVGL "S:" path when found on disk.
//
// `lv_img_set_src` accepts both forms; the caller hands the result to LVGL
// without branching. Widgets that need the older path-string API for an
// asset-existence probe (button_widget heap gate, preloader) keep using
// `path()` / `exists()`.

namespace IconAssets {

// Resolve `iconName` to the cheapest LVGL source available:
//   - baked dsc pointer when the icon is compiled into flash, OR
//   - SPIFFS LVGL FS path string ("S:/assets/...") when the .bin is on disk,
//   - nullptr when no asset exists for the name.
//
// Both non-null forms are valid arguments to `lv_img_set_src()`. The decoder
// picks the right backend automatically based on the first byte of the
// source (LVGL's `lv_img_src_get_type`).
const void *resolveSource(const char *iconName);

// LVGL FS path for the icon, "" if no mapping or the underlying .bin file is
// missing on the storage backend. Probes storage synchronously so callers can
// rely on "non-empty result == file exists and lv_img_set_src will succeed".
//
// Prefer `resolveSource()` for new code — `path()` returns an empty string
// for baked icons even though they exist (no SPIFFS file).
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
