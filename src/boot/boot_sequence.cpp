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
#include "ui/font_manager.h"

#if !APP_SIMULATION_MODE
    #include "can/can_manager.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

// Diagnostic — log free heap and largest contiguous block at a named boot stage.
// Helps pinpoint memory pressure without needing a debugger.
static void logHeap(const char *stage) {
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("HEAP", "%s: free=%u largest=%u", stage,
             static_cast<unsigned>(free), static_cast<unsigned>(largest));
}

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

// Build the dark splash background — used by both normal boot and error screen.
// Uses LV_FONT_DEFAULT (montserrat_14, always in flash) because fonts from SD
// are not yet loaded at this point in the boot sequence.
static lv_obj_t *buildSplashBase() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CANShift");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, "v" APP_VERSION_STR);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x444444), 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, -20);

    return scr;
}

static void showSplash() {
    lv_obj_t *scr = buildSplashBase();

    static constexpr int16_t BAR_W = 280;
    static constexpr int16_t BAR_H = 4;
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "Starting\xe2\x80\xa6");
    lv_obj_set_style_text_color(status, lv_color_hex(0x444444), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 52);

    s_splashBar = bar;
    s_splashStatus = status;

    lv_task_handler();
}

// Shown when the SD card is absent or fails to mount.
// Halts — the dashboard cannot run without the SD card.
static void showSDError() {
    buildSplashBase();
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, "!");
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF4444), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *msg = lv_label_create(scr);
    lv_label_set_text(msg, "SD card missing\nInsert SD and restart");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, 280);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 40);

    lv_task_handler();
    LOG_ERROR("BOOT", "SD card missing — dashboard halted");

    while (true) {
        lv_task_handler();
        delay(100);
    }
}

// Call between each boot step to advance the bar and update the status text.
static void updateSplash(const char *status, uint8_t pct) {
    if (s_splashBar)
        lv_bar_set_value(s_splashBar, pct, LV_ANIM_OFF);
    if (s_splashStatus)
        lv_label_set_text(s_splashStatus, status);
    lv_task_handler();
}

// Returns false if SD is absent — caller must halt.
static bool initStorage() {
    LOG_INFO("BOOT", "Initializing SD card...");
    if (!StorageDriver::init()) {
        return false;
    }
    LvglFsDriver::init();
    FontManager::init();
    return true;
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
    logHeap("entry");
    // 1. Display + LVGL must come early so we can show a splash
    initDisplayAndLVGL();
    logHeap("after lv_init");
    showSplash(); // 0 %

    // 2. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    updateSplash("Initializing touch\xe2\x80\xa6", 15);

    // 3. Storage — fatal if SD missing
    updateSplash("Checking SD card\xe2\x80\xa6", 20);
    if (!initStorage()) {
        showSDError(); // halts
    }
    updateSplash("SD ready\xe2\x80\xa6", 35);

    // 4. Config
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");
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
    logHeap("before buildUI");
    buildUI();
    logHeap("after buildUI");
    updateSplash("Ready", 100);

    LOG_INFO("BOOT", "Boot sequence complete");
}
