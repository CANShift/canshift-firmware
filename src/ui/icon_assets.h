#pragma once
// icon_assets.h — Maps SensorIconName keys (from canshift-core) to LVGL
// asset paths and Unicode fallback glyphs.
//
// Studio renders sensor icons as SVG components; the firmware loads matching
// .bin (RGB565) files from the SD card under "/assets/sensor_<name>.bin".
// When the asset is absent (e.g. SD missing, or freshly flashed device that
// never received the asset bundle) the widget falls back to a LV_SYMBOL_*
// glyph so the widget always renders something.

#include <lvgl.h>

namespace IconAssets {

// LVGL FS path for the icon, "" if no mapping or the underlying .bin file is
// missing on the storage backend. Probes the SD synchronously so callers can
// rely on "non-empty result == file exists and lv_img_set_src will succeed".
const char *path(const char *iconName);

// Probe an arbitrary LVGL FS path (e.g. user-supplied "S:/assets/custom.bin")
// and report whether the backing file exists on storage. Used by widgets that
// accept a free-form iconPath in addition to the iconName lookup.
bool exists(const char *lvglPath);

// Single-codepoint LVGL symbol used when no .bin asset is available. Always
// returns a non-null UTF-8 string (defaults to LV_SYMBOL_WARNING).
const char *fallbackGlyph(const char *iconName);

} // namespace IconAssets
