#include "icon_assets.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"
#include "icon_assets_baked.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>

namespace IconAssets {

namespace {

struct IconEntry {
    const char *name;
    const char *path;
};

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

const char *stripDriveLetter(const char *lvglPath) {
    if (!lvglPath || lvglPath[0] == '\0')
        return nullptr;

    if (lvglPath[1] == ':')
        return lvglPath + 2;

    return lvglPath;
}

} // namespace

const void *resolveSource(const char *iconName) {

    if (const lv_img_dsc_t *baked = IconAssetsBaked::resolve(iconName))
        return baked;
    const char *p = path(iconName);
    return p[0] != '\0' ? p : nullptr;
}

const char *path(const char *iconName) {
    const IconEntry *e = find(iconName);
    if (!e || e->path[0] == '\0')
        return "";

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

namespace {

constexpr uint8_t SEEN_CAP = 32;
constexpr uint8_t SEEN_LEN = 24;

bool rememberSeen(char seen[SEEN_CAP][SEEN_LEN], uint8_t &count, const char *name) {
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(seen[i], name) == 0)
            return false;
    }
    if (count >= SEEN_CAP)
        return false;
    strlcpy(seen[count], name, SEEN_LEN);
    ++count;
    return true;
}

void preloadIconNameOnce(char seen[SEEN_CAP][SEEN_LEN], uint8_t &seenCount, const char *iconName) {
    if (!iconName || iconName[0] == '\0')
        return;
    if (!rememberSeen(seen, seenCount, iconName))
        return;

    if (IconAssetsBaked::resolve(iconName) != nullptr)
        return;
    preload(path(iconName));
}

} // namespace

void preload(const char *lvglPath) {
    if (!lvglPath || lvglPath[0] == '\0')
        return;
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < LVGL_FS_MIN_HEAP_BYTES) {
        LOG_WARN("ICON", "Skip preload of %s — heap largest=%u too low", lvglPath,
                 static_cast<unsigned>(largest));
        return;
    }

    _lv_img_cache_open(lvglPath, lv_color_black(), 0);
}

void preloadDashboardAssets() {

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded)
        return;

    char seen[SEEN_CAP][SEEN_LEN] = {{0}};
    uint8_t seenCount = 0;

    for (uint8_t p = 0; p < dash.pageCount; ++p) {
        const CfgPage &page = dash.pages[p];
        for (uint8_t w = 0; w < page.widgetCount; ++w) {
            const CfgWidget &widget = page.widgets[w];
            switch (widget.type) {
                case WidgetType::WARNING:
                    preloadIconNameOnce(seen, seenCount, widget.warning.iconName);
                    break;
                case WidgetType::BUTTON:
                    preloadIconNameOnce(seen, seenCount, widget.button.iconName);

                    if (widget.button.iconPath[0] != '\0') {
                        char lvglPath[64];
                        const char *prefix = (widget.button.iconPath[0] == '/') ? "" : "/";
                        snprintf(lvglPath, sizeof(lvglPath), "S:%s%s", prefix,
                                 widget.button.iconPath);
                        if (exists(lvglPath) &&
                            rememberSeen(seen, seenCount, widget.button.iconPath))
                            preload(lvglPath);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    LOG_INFO("ICON", "Preloaded %u dashboard icon(s) (theme +2)", static_cast<unsigned>(seenCount));
}

} // namespace IconAssets
