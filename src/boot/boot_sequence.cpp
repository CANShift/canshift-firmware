// boot_sequence.cpp — Power-on initialization sequence

#include "boot_sequence.h"
#include "app_config.h"
#include "board_config.h"

#include "diag/logger.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/usb/usb_comm.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "runtime/alert_engine.h"
#include "ui/page_manager.h"
#include "ui/theme_manager.h"

#if !APP_SIMULATION_MODE
#include "can/can_manager.h"
#endif

#include <lvgl.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void initDisplayAndLVGL() {
    LOG_INFO("BOOT", "Initializing display...");
    DisplayDriver::init();

    LOG_INFO("BOOT", "Initializing LVGL...");
    lv_init();
    DisplayDriver::registerWithLVGL();

    LOG_INFO("BOOT", "Display + LVGL ready");
}

static void showSplash() {
    // Create a minimal splash screen directly on LVGL default display
    lv_obj_t* splash = lv_obj_create(lv_scr_act());
    lv_obj_set_size(splash, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(splash, lv_color_hex(0x111111), 0);

    lv_obj_t* label = lv_label_create(splash);
    lv_label_set_text(label, "CANShift\nv" APP_VERSION_STR);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_task_handler();
}

static void initStorage() {
    LOG_INFO("BOOT", "Initializing storage...");
    if (!StorageDriver::init()) {
        LOG_ERROR("BOOT", "Storage init failed — no config will be loaded");
        // Non-fatal: firmware can run with defaults
    }
}

static void loadConfig() {
    LOG_INFO("BOOT", "Loading configuration...");
    ConfigLoader::LoadResult result = ConfigLoader::loadAll();

    if (!result.dashboardOk) {
        LOG_WARN("BOOT", "dashboard.json missing or invalid — using built-in defaults");
    }
    if (!result.signalsOk) {
        LOG_WARN("BOOT", "signals.json missing or invalid — CAN parsing disabled");
    }
    if (!result.themeOk) {
        LOG_WARN("BOOT", "theme.json missing or invalid — using default theme");
    }
}

static void buildUI() {
    LOG_INFO("BOOT", "Building UI...");
    ThemeManager::apply();
    PageManager::init();
    PageManager::navigateTo(PageManager::getDefaultPageId());
    LOG_INFO("BOOT", "UI ready");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BootSequence::run() {
    // 1. Display + LVGL must come early so we can show a splash
    initDisplayAndLVGL();
    showSplash();

    // 2. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();

    // 3. Storage
    initStorage();

    // 4. Config
    loadConfig();

    // 5. Runtime
    SignalStore::init();
    AlertEngine::init();

    // 6. CAN hardware (skip in simulation mode)
#if !APP_SIMULATION_MODE
    LOG_INFO("BOOT", "Initializing CAN/TWAI...");
    CanManager::initHardware();
#else
    LOG_INFO("BOOT", "Simulation mode — skipping CAN init");
#endif

    // 7. USB comm
    LOG_INFO("BOOT", "Initializing USB comm...");
    UsbComm::init();

    // 8. Build the UI from config
    buildUI();

    LOG_INFO("BOOT", "Boot sequence complete");
}
