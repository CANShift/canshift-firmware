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

} // namespace IconAssets
