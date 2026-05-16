// icon_assets.cpp — Sensor icon name → SPIFFS asset path.

#include "icon_assets.h"
#include "hal/storage/storage_driver.h"

#include <string.h>

namespace IconAssets {

namespace {

struct IconEntry {
    const char *name;
    const char *path; // SPIFFS path
};

// SensorIconName values (canshift-core/src/types/dashboard.ts).
// .bin assets are expected at "S:/assets/sensor_<name>.bin" on SPIFFS;
// when missing the widget skips the icon entirely (see #681 — the LVGL
// symbol fallback was removed because Orbitron does not cover that range).
constexpr IconEntry kIcons[] = {
    {"rpm", "S:/assets/sensor_rpm.bin"},
    {"speed", "S:/assets/sensor_speed.bin"},
    {"coolant", "S:/assets/sensor_coolant.bin"},
    {"oil_pressure", "S:/assets/sensor_oil_pressure.bin"},
    {"oil_temp", "S:/assets/sensor_oil_temp.bin"},
    {"battery", "S:/assets/sensor_battery.bin"},
    {"fuel", "S:/assets/sensor_fuel.bin"},
    {"afr", "S:/assets/sensor_afr.bin"},
    {"boost", "S:/assets/sensor_boost.bin"},
    {"throttle", "S:/assets/sensor_throttle.bin"},
    {"iat", "S:/assets/sensor_iat.bin"},
    {"gear", "S:/assets/sensor_gear.bin"},
    {"timer", "S:/assets/sensor_timer.bin"},
    {"warning", "S:/assets/sensor_warning.bin"},
    {"flame", "S:/assets/sensor_flame.bin"},
    {"turbo", "S:/assets/sensor_turbo.bin"},
    {"engine", "S:/assets/sensor_engine.bin"},
    {"brake", "S:/assets/sensor_brake.bin"},
    {"launch", "S:/assets/sensor_launch.bin"},
    {"traction", "S:/assets/sensor_traction.bin"},
    {"map_icon", "S:/assets/sensor_map_icon.bin"},
    {"exhaust", "S:/assets/sensor_exhaust.bin"},
    {"cog", "S:/assets/sensor_cog.bin"},
};

const IconEntry *find(const char *iconName) {
    if (!iconName || iconName[0] == '\0')
        return nullptr;
    for (const auto &e : kIcons) {
        if (strcmp(e.name, iconName) == 0)
            return &e;
    }
    return nullptr;
}

// Strip the "S:" drive prefix (or "/<letter>:" variants we never use) so the
// raw path can be probed via StorageDriver::fileExists. Returns nullptr when
// the input doesn't start with the expected drive letter — defensive against
// malformed config.
const char *stripDriveLetter(const char *lvglPath) {
    if (!lvglPath || lvglPath[0] == '\0')
        return nullptr;
    // Drive letter + ':' prefix (e.g. "S:/foo") — strip both, leaving "/foo".
    if (lvglPath[1] == ':')
        return lvglPath + 2;
    // No prefix — assume it's already a raw FS path.
    return lvglPath;
}

} // namespace

const char *path(const char *iconName) {
    const IconEntry *e = find(iconName);
    if (!e || e->path[0] == '\0')
        return "";
    // Probe storage before returning the path: when the .bin isn't on
    // SPIFFS the LVGL image draw silently no-ops, so the caller would
    // render an empty box. Returning "" here lets the widget skip icon
    // rendering instead. Cost: one SPIFFS stat per icon at UI build time.
    if (!exists(e->path))
        return "";
    return e->path;
}

bool exists(const char *lvglPath) {
    const char *raw = stripDriveLetter(lvglPath);
    if (!raw || raw[0] == '\0')
        return false;
    return StorageDriver::fileExists(raw);
}

} // namespace IconAssets
