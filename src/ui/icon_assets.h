#pragma once
// icon_assets.h — Maps SensorIconName keys (from canshift-core) to LVGL
// asset paths and Unicode fallback glyphs.
//
// Studio renders sensor icons as SVG components; the firmware loads matching
// .bin (RGB565) files from the SD card under "/assets/sensor_<name>.bin".
// When the asset is absent the widget falls back to a LV_SYMBOL_* glyph so
// the widget always renders something.

#include <lvgl.h>

namespace IconAssets {

// LVGL FS path for the icon, "" if no mapping. Caller may probe-load and
// fall back to fallbackGlyph() when the file isn't present on the SD.
const char *path(const char *iconName);

// Single-codepoint LVGL symbol used when no .bin asset is available. Always
// returns a non-null UTF-8 string (defaults to LV_SYMBOL_WARNING).
const char *fallbackGlyph(const char *iconName);

} // namespace IconAssets
