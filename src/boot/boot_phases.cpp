#include "boot_phases.h"

#include "app_config.h"
#include "board_config.h"

#include "boot/default_fonts.h"
#include "config/config_loader.h"
#include "config/default_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/display/display_driver.h"
#include "hal/memory/psram.h"
#include "hal/storage/lvgl_fs_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/touch/touch_driver.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"

#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <lvgl.h>

namespace BootPhases {

namespace {

void loadConfig() {
    LOG_INFO("BOOT", "Loading configuration...");
    ConfigLoader::LoadResult result = ConfigLoader::loadAll();

    if (!result.dashboardOk) {
        LOG_WARN("BOOT", "dashboard.json missing or invalid — using built-in defaults");
    }
    if (!result.signalsOk) {
        LOG_WARN("BOOT", "signals.json missing or invalid — CAN parsing disabled");
    }
}

} // namespace

void logHeap(const char *stage) {
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t minFree = ESP.getMinFreeHeap();
    if (canshift::hal::memory::isPsramAvailable()) {
        const uint32_t freePsram = static_cast<uint32_t>(canshift::hal::memory::getFreePsram());
        LOG_INFO("HEAP", "%s: free=%u largest=%u min=%u psram_free=%u", stage,
                 static_cast<unsigned>(free), static_cast<unsigned>(largest),
                 static_cast<unsigned>(minFree), static_cast<unsigned>(freePsram));
        return;
    }
    LOG_INFO("HEAP", "%s: free=%u largest=%u min=%u", stage, static_cast<unsigned>(free),
             static_cast<unsigned>(largest), static_cast<unsigned>(minFree));
}

void silenceFrameworkLogNoise() {
    esp_log_level_set("nvs", ESP_LOG_WARN);
#if !APP_DEBUG_BUILD
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
}

void initPsramAndLogEntry() {
    canshift::hal::memory::initPsram();
    logHeap("entry");
}

void initTaskWatchdog() {
    constexpr uint32_t kWdtTimeoutS = (TASK_WDT_TIMEOUT_MS + 999U) / 1000U;
    const esp_err_t wdtErr = esp_task_wdt_init(kWdtTimeoutS, true);
    if (wdtErr != ESP_OK) {
        LOG_ERROR("BOOT", "Task WDT init failed: %d — continuing without WDT",
                  static_cast<int>(wdtErr));
        return;
    }
    LOG_INFO("BOOT", "Task WDT armed (%u s)", static_cast<unsigned>(kWdtTimeoutS));
}

void initLvglMemoryPool() {
    LOG_INFO("BOOT", "Calling lv_init()...");
    lv_init();
    LOG_INFO("BOOT", "lv_init() returned");
    logHeap("after lv_init");
}

bool mountStorageOrLogError() {
    const bool storageOk = StorageDriver::init();
    if (storageOk) {
        LOG_INFO("BOOT", "Storage mounted");
    } else {
        LOG_ERROR("BOOT", "Storage mount failed — running with defaults");
        ErrorStore::push(ERROR_SRC_SYSTEM, "MOUNT_FAIL", "Storage offline — config not persisted");
    }
    logHeap("after storage");
    return storageOk;
}

void provisionDefaultConfigsIfNeeded(bool storageOk) {
#if DEFAULT_CONFIG_PROVISION_ENABLED
    if (!storageOk)
        return;
    LOG_INFO("BOOT", "Provisioning default configs (if needed)...");
    const DefaultConfig::ProvisionResult pr = DefaultConfig::provisionMissingFiles();
    if (pr.written > 0)
        LOG_INFO("BOOT", "Provisioned %u default config file(s)",
                 static_cast<unsigned>(pr.written));
    if (pr.failed > 0)
        LOG_WARN("BOOT",
                 "Default-config provision failed for %u file(s) — "
                 "continuing with whatever is on storage",
                 static_cast<unsigned>(pr.failed));
#else
    (void)storageOk;
#endif
}

void loadConfigWithHeapBracket() {
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");
}

void initBleEarlyIfEnabled() {
#if APP_BLE_ENABLED
    BleServer::earlyInit();
    logHeap("after BLE early init");
#endif
}

void initDisplayHardware() {
    LOG_INFO("BOOT", "Initializing display...");
    DisplayDriver::init();
    LOG_INFO("BOOT", "Display driver up");

    LOG_INFO("BOOT", "Registering display with LVGL...");
    DisplayDriver::registerWithLVGL();
    LOG_INFO("BOOT", "Display + LVGL ready");
}

void initTouchHardware() {
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    LOG_INFO("BOOT", "Touch ready");
}

void initLvglFsIfStorageOk(bool storageOk) {
    if (storageOk) {
        LvglFsDriver::init();
    }
}

void provisionDefaultFontsIfNeeded(bool storageOk) {
    if (!storageOk) {
        logHeap("before FontManager");
        return;
    }
    LOG_INFO("BOOT", "Provisioning default fonts (if needed)...");
    const DefaultFonts::ProvisionResult fr = DefaultFonts::provisionMissingFiles();
    if (fr.written > 0)
        LOG_INFO("BOOT", "Provisioned %u default font file(s)", static_cast<unsigned>(fr.written));
    if (fr.failed > 0)
        LOG_WARN("BOOT",
                 "Default-font provision failed for %u file(s) — "
                 "FontManager will fall back to built-in glyph",
                 static_cast<unsigned>(fr.failed));
    logHeap("before FontManager");
}

void initFontManagerWithHeapLog() {
    LOG_INFO("BOOT", "Initializing FontManager...");
    FontManager::init();
    LOG_INFO("BOOT", "FontManager ready");
    logHeap("after FontManager");
}

void preloadIconsWithHeapLog() {
    LOG_INFO("BOOT", "Preloading dashboard icons...");
    IconAssets::preloadDashboardAssets();
    logHeap("after icon preload");
}

} // namespace BootPhases
