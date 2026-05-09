// icon_assets.cpp — Sensor icon name → asset path / fallback glyph.

#include "icon_assets.h"
#include "hal/storage/storage_driver.h"

#include <string.h>

namespace IconAssets {

namespace {

struct IconEntry {
    const char *name;
    const char *path;     // SPIFFS path, "" if no shipped asset yet
    const char *fallback; // LVGL symbol used when path missing or empty
};

// SensorIconName values (canshift-core/src/types/dashboard.ts).
// .bin assets are expected at "S:/assets/sensor_<name>.bin" on SPIFFS;
// when missing, the fallback glyph keeps the widget visible.
constexpr IconEntry kIcons[] = {
    {"rpm",          "S:/assets/sensor_rpm.bin",          LV_SYMBOL_LOOP},
    {"speed",        "S:/assets/sensor_speed.bin",        LV_SYMBOL_RIGHT},
    {"coolant",      "S:/assets/sensor_coolant.bin",      LV_SYMBOL_TINT},
    {"oil_pressure", "S:/assets/sensor_oil_pressure.bin", LV_SYMBOL_DRIVE},
    {"oil_temp",     "S:/assets/sensor_oil_temp.bin",     LV_SYMBOL_DRIVE},
    {"battery",      "S:/assets/sensor_battery.bin",      LV_SYMBOL_BATTERY_FULL},
    {"fuel",         "S:/assets/sensor_fuel.bin",         LV_SYMBOL_GPS},
    {"afr",          "S:/assets/sensor_afr.bin",          LV_SYMBOL_CHARGE},
    {"boost",        "S:/assets/sensor_boost.bin",        LV_SYMBOL_UP},
    {"throttle",     "S:/assets/sensor_throttle.bin",     LV_SYMBOL_PLAY},
    {"iat",          "S:/assets/sensor_iat.bin",          LV_SYMBOL_DOWNLOAD},
    {"gear",         "S:/assets/sensor_gear.bin",         LV_SYMBOL_SETTINGS},
    {"timer",        "S:/assets/sensor_timer.bin",        LV_SYMBOL_REFRESH},
    {"warning",      "S:/assets/sensor_warning.bin",      LV_SYMBOL_WARNING},
    {"flame",        "S:/assets/sensor_flame.bin",        LV_SYMBOL_CHARGE},
    {"turbo",        "S:/assets/sensor_turbo.bin",        LV_SYMBOL_REFRESH},
    {"engine",       "S:/assets/sensor_engine.bin",       LV_SYMBOL_SETTINGS},
    {"brake",        "S:/assets/sensor_brake.bin",        LV_SYMBOL_STOP},
    {"launch",       "S:/assets/sensor_launch.bin",       LV_SYMBOL_PLAY},
    {"traction",     "S:/assets/sensor_traction.bin",     LV_SYMBOL_REFRESH},
    {"map_icon",     "S:/assets/sensor_map_icon.bin",     LV_SYMBOL_LIST},
    {"exhaust",      "S:/assets/sensor_exhaust.bin",      LV_SYMBOL_AUDIO},
    {"cog",          "S:/assets/sensor_cog.bin",          LV_SYMBOL_SETTINGS},
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
    // render an empty box. Returning "" here lets the widget activate the
    // glyph fallback path instead. Cost: one SPIFFS stat per icon at UI
    // build time.
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

const char *fallbackGlyph(const char *iconName) {
    const IconEntry *e = find(iconName);
    return e ? e->fallback : LV_SYMBOL_WARNING;
}

} // namespace IconAssets
