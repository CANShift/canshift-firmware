// icon_assets.cpp — Sensor icon name → SPIFFS asset path.

#include "icon_assets.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>

// `_lv_img_cache_open` is declared transitively through `<lvgl.h>` (the
// umbrella pulls in `src/draw/lv_img_cache.h`). Calling it with a path
// opens the image via the decoder and pins the decoded data in the cache
// until evicted — see preload() below.

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

namespace {

// Heap guard mirrors lvgl_fs_driver.cpp via the shared LVGL_FS_MIN_HEAP_BYTES
// constant (include/app_config.h): if the largest free block is below the
// FS-open threshold, decoding will fail anyway and may trip the newlib
// __sfp() abort. Skip the preload in that case — the asset will be retried
// on demand by the widget layer. Using the shared constant guarantees the
// preload and on-demand paths agree on the gate threshold (#1242 — they
// previously diverged, with preload at 512 and the widget gate at 768, so
// a path could be preloaded into the cache only to have the widget refuse
// to render it from the same heap moments later).

// In-place "have we seen this name" tracker — avoids preloading the same
// asset twice when several widgets reference it. Sized to the IconAssets
// catalog (23) + a margin for free-form iconPath strings.
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
    // `_lv_img_cache_open` opens the image via the decoder and leaves it in
    // the cache. The entry stays alive until the cache evicts it to make
    // room for a newer image. With LV_IMG_CACHE_DEF_SIZE sized to cover a
    // typical dashboard, preloaded entries survive every page rebuild and
    // theme toggle that follows.
    _lv_img_cache_open(lvglPath, lv_color_black(), 0);
}

void preloadDashboardAssets() {
    // Theme icons — always needed regardless of dashboard contents.
    preload("S:/assets/icon_day.bin");
    preload("S:/assets/icon_night.bin");

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
                case WidgetType::BAR:
                    preloadIconNameOnce(seen, seenCount, widget.bar.iconName);
                    break;
                case WidgetType::WARNING:
                    preloadIconNameOnce(seen, seenCount, widget.warning.iconName);
                    break;
                case WidgetType::BUTTON:
                    preloadIconNameOnce(seen, seenCount, widget.button.iconName);
                    // Buttons can also reference a free-form iconPath in the
                    // config — preload that too. The path stored in config
                    // is the raw SPIFFS path; wrap in "S:" for LVGL FS.
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
