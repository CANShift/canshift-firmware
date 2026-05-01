// boot_sequence.cpp — Power-on initialization sequence

#include "boot_sequence.h"
#include "app_config.h"
#include "board_config.h"

#include "diag/logger.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/storage/lvgl_fs_driver.h"
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

// ---------------------------------------------------------------------------
// Splash screen — title + progress bar + status label
// Objects stored so updateSplash() can drive them step by step.
// ---------------------------------------------------------------------------

namespace {
static lv_obj_t *s_splashBar = nullptr;
static lv_obj_t *s_splashStatus = nullptr;
} // namespace

static void showSplash() {
    lv_obj_t *scr = lv_scr_act();

    // Full-screen background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // App name — bold, brand red, centered slightly above middle
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CANShift");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    // Version — dim grey, right below the title
    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, "v" APP_VERSION_STR);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, 4);

    // Progress bar — 280 px wide, 4 px tall, below center
    static constexpr int16_t BAR_W = 280;
    static constexpr int16_t BAR_H = 4;
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 48);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Bar track (background)
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    // Bar indicator (filled portion)
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

    // Status label — small, dim, below the bar
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "Starting…");
    lv_obj_set_style_text_color(status, lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 66);

    s_splashBar = bar;
    s_splashStatus = status;

    lv_task_handler();
}

// Call between each boot step to advance the bar and update the status text.
static void updateSplash(const char *status, uint8_t pct) {
    if (s_splashBar)
        lv_bar_set_value(s_splashBar, pct, LV_ANIM_OFF);
    if (s_splashStatus)
        lv_label_set_text(s_splashStatus, status);
    lv_task_handler();
}

static void initStorage() {
    LOG_INFO("BOOT", "Initializing storage...");
    if (!StorageDriver::init()) {
        LOG_ERROR("BOOT", "Storage init failed — no config will be loaded");
        // Non-fatal: firmware can run with defaults
    }
    // Register LVGL FS driver so image widgets can load BMPs from SPIFFS
    LvglFsDriver::init();
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
    showSplash(); // 0 %

    // 2. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    updateSplash("Initializing touch\xe2\x80\xa6", 15);

    // 3. Storage
    initStorage();
    updateSplash("Loading storage\xe2\x80\xa6", 30);

    // 4. Config
    loadConfig();
    updateSplash("Applying config\xe2\x80\xa6", 55);

    // 5. Runtime
    SignalStore::init();
    AlertEngine::init();
    updateSplash("Starting runtime\xe2\x80\xa6", 65);

    // 6. CAN hardware (skip in simulation mode)
#if !APP_SIMULATION_MODE
    LOG_INFO("BOOT", "Initializing CAN/TWAI...");
    CanManager::initHardware();
    updateSplash("CAN ready", 80);
#else
    LOG_INFO("BOOT", "Simulation mode — skipping CAN init");
    updateSplash("Simulation mode", 80);
#endif

    // 7. USB comm
    LOG_INFO("BOOT", "Initializing USB comm...");
    UsbComm::init();
    updateSplash("USB ready", 88);

    // 8. Build the UI from config
    buildUI();
    updateSplash("Ready", 100);

    LOG_INFO("BOOT", "Boot sequence complete");
}
