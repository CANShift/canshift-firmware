// boot_sequence.cpp — Power-on initialization sequence

#include "boot_sequence.h"
#include "app_config.h"
#include "board_config.h"
#include "hardware_profile.h"

#include "diag/logger.h"
#include "diag/error_store.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/storage/lvgl_fs_driver.h"
#include "hal/usb/usb_comm.h"
#include "boot/default_fonts.h"
#include "config/config_loader.h"
#include "config/default_config.h"
#include "runtime/signal_store.h"
#include "runtime/alert_engine.h"
#include "runtime/timer_service.h"
#include "ui/page_manager.h"
#include "ui/theme_manager.h"
#include "ui/font_manager.h"
#include "ui/top_bar.h"

#if !APP_SIMULATION_MODE
    #include "can/can_manager.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl.h>

// Diagnostic — log free heap, largest contiguous LVGL-relevant block, and
// the lifetime low-watermark at a named boot stage. Helps pinpoint memory
// pressure without needing a debugger. MALLOC_CAP_INTERNAL is the pool LVGL
// allocates from, so it is the metric that actually tracks UI headroom.
static void logHeap(const char *stage) {
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t minFree = ESP.getMinFreeHeap();
    LOG_INFO("HEAP", "%s: free=%u largest=%u min=%u", stage, static_cast<unsigned>(free),
             static_cast<unsigned>(largest), static_cast<unsigned>(minFree));
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void initDisplayAndLVGL() {
    LOG_INFO("BOOT", "Initializing display...");
    DisplayDriver::init();
    LOG_INFO("BOOT", "Display driver up");

    LOG_INFO("BOOT", "Calling lv_init()...");
    lv_init();
    LOG_INFO("BOOT", "lv_init() returned");
    logHeap("after lv_init");

    LOG_INFO("BOOT", "Registering display with LVGL...");
    DisplayDriver::registerWithLVGL();
    LOG_INFO("BOOT", "Display registered with LVGL");

    LOG_INFO("BOOT", "Display + LVGL ready");
}

// ---------------------------------------------------------------------------
// Splash screen — title + version + progress bar + status label.
// No banner image (looked off on this small panel) — text-only is enough.
// ---------------------------------------------------------------------------

namespace {
static lv_obj_t *s_splashBar = nullptr;
static lv_obj_t *s_splashStatus = nullptr;
static lv_obj_t *s_splashTitle = nullptr;
} // namespace

// Build the dark splash background.
static lv_obj_t *buildSplashBase() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Title uses the static built-in Orbitron 14 at first paint; FontManager::init()
    // upgrades it to Orbitron Black 32 (#544) once SPIFFS-backed fonts are loaded.
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CANShift");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);
    s_splashTitle = title;

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, "v" APP_VERSION_STR);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x666666), 0);
    lv_obj_align(ver, LV_ALIGN_CENTER, 0, -10);

    return scr;
}

static void showSplash() {
    lv_obj_t *scr = buildSplashBase();

    static constexpr int16_t BAR_MARGIN_PX = 20;
    static constexpr int16_t BAR_W = HW_DISPLAY_WIDTH - 2 * BAR_MARGIN_PX;
    static_assert(BAR_W == 280, "splash bar width regression for CrowPanel 2.8\"");
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
    lv_label_set_text(status, "Starting...");
    lv_obj_set_style_text_color(status, lv_color_hex(0x666666), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 52);

    s_splashBar = bar;
    s_splashStatus = status;

    lv_task_handler();
}

// Advance the bar and update the status text between init steps.
// lv_refr_now flushes synchronously so the bar visibly progresses even when
// boot stages take only a few ms.
static void updateSplash(const char *status, uint8_t pct) {
    if (s_splashBar)
        lv_bar_set_value(s_splashBar, pct, LV_ANIM_OFF);
    if (s_splashStatus)
        lv_label_set_text(s_splashStatus, status);
    lv_refr_now(NULL);
}

// Returns true if the storage came up cleanly. On failure the boot continues
// with built-in defaults — the device stays reachable over USB.
//
// FontManager::init() is intentionally NOT called here — it must run AFTER
// DefaultFonts::provisionMissingFiles() so a freshly-flashed device finds
// the .bin files on SPIFFS rather than seeing every lv_font_load() return
// NULL (issue #467).
static bool initStorage() {
    // Storage attempt is logged by StorageDriver::init() under the STORAGE tag
    // with partition geometry — duplicating it under BOOT adds noise.
    const bool ok = StorageDriver::init();
    if (!ok) {
        return false;
    }
    LvglFsDriver::init();
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
    LOG_INFO("BOOT", "Applying theme...");
    ThemeManager::apply();
    LOG_INFO("BOOT", "Initializing PageManager...");
    PageManager::init();
    LOG_INFO("BOOT", "Navigating to default page...");
    PageManager::navigateTo(PageManager::getDefaultPageId());
    LOG_INFO("BOOT", "UI ready");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Minimum splash visibility — boot tends to finish in < 1 s, which feels
// twitchy and gives the user no time to read the version. Hold at least this
// long before handing the screen over to the dashboard.
static constexpr uint32_t SPLASH_MIN_MS = 2000;

void BootSequence::run() {
    // Boot is single-threaded — taskUI is created in main.cpp after this
    // returns, so LVGL calls below do not need g_lvglMutex.

    // Silence ESP-IDF NVS error logs on first boot. Preferences::begin(ns,
    // /*readOnly=*/true) on a namespace that doesn't yet exist (touch cal,
    // settings) emits "[E] nvs_open failed: NOT_FOUND" via ESP-IDF's NVS log
    // tag. The read-fail is expected and the caller already handles it with a
    // sensible default — the ERROR-level log is just noise. Demoting the tag
    // to WARN keeps real NVS errors visible (#42).
    esp_log_level_set("nvs", ESP_LOG_WARN);

    const uint32_t bootStartMs = millis();

    logHeap("entry");
    // 1. Display + LVGL must come early so we can show a splash
    initDisplayAndLVGL();

    LOG_INFO("BOOT", "Showing splash...");
    showSplash(); // 0 %
    LOG_INFO("BOOT", "Splash visible");
    logHeap("after splash");

    // 2. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    LOG_INFO("BOOT", "Touch ready");
    updateSplash("Initializing touch...", 15);

    // 3. Storage — degrade (don't halt) on mount failure so the studio can
    //    still reach the device over USB and the user can see a default
    //    dashboard.
    updateSplash("Initializing storage...", 20);
    const bool storageOk = initStorage();
    if (storageOk) {
        LOG_INFO("BOOT", "Storage mounted");
        updateSplash("Storage ready", 35);
    } else {
        LOG_ERROR("BOOT", "Storage mount failed — running with defaults");
        // Persist the failure on the dashboard error bar — the splash message
        // is dismissed after ~2 s and the user otherwise has no on-device
        // indication that their config was not loaded from flash.
        ErrorStore::push(ERROR_SRC_SYSTEM, "MOUNT_FAIL",
                         "Storage offline — config not persisted");
        updateSplash("Storage error — defaults", 35);
    }
    logHeap("after storage");

    // 3.5 Provision default configs on a fresh / empty SPIFFS before
    //     loadConfig reads. Writes only when target is missing AND no .bak
    //     exists, or when target is empty. Never overwrites user data.
#if DEFAULT_CONFIG_PROVISION_ENABLED
    if (storageOk) {
        LOG_INFO("BOOT", "Provisioning default configs (if needed)...");
        const DefaultConfig::ProvisionResult pr = DefaultConfig::provisionMissingFiles();
        if (pr.written > 0) {
            LOG_INFO("BOOT", "Provisioned %u default config file(s)",
                     static_cast<unsigned>(pr.written));
            updateSplash("Provisioning defaults...", 45);
        }
        if (pr.failed > 0) {
            LOG_WARN("BOOT",
                     "Default-config provision failed for %u file(s) — "
                     "continuing with whatever is on storage",
                     static_cast<unsigned>(pr.failed));
        }
    }
#endif

    // 3.6 Provision the 8 Orbitron .bin fonts on a fresh / empty SPIFFS
    //     BEFORE FontManager::init() so lv_font_load() finds them. Writes
    //     only when target is missing — never overwrites existing files.
    if (storageOk) {
        LOG_INFO("BOOT", "Provisioning default fonts (if needed)...");
        const DefaultFonts::ProvisionResult fr = DefaultFonts::provisionMissingFiles();
        if (fr.written > 0) {
            LOG_INFO("BOOT", "Provisioned %u default font file(s)",
                     static_cast<unsigned>(fr.written));
            updateSplash("Provisioning fonts...", 48);
        }
        if (fr.failed > 0) {
            LOG_WARN("BOOT",
                     "Default-font provision failed for %u file(s) — "
                     "FontManager will fall back to built-in glyph",
                     static_cast<unsigned>(fr.failed));
        }
    }
    logHeap("before FontManager");

    // 3.7 Now that .bin fonts are guaranteed present (when storage works),
    //     load them into LVGL. Failures here only degrade typography — the
    //     fallback glyph keeps text readable.
    LOG_INFO("BOOT", "Initializing FontManager...");
    FontManager::init();
    LOG_INFO("BOOT", "FontManager ready");
    logHeap("after FontManager");

    // Upgrade splash title to Orbitron Black 32 now that SPIFFS-backed fonts
    // are registered with LVGL (#544). Until this point the title rendered
    // in the static built-in Orbitron 14 fallback.
    if (s_splashTitle != nullptr) {
        const lv_font_t *bigTitleFont = FontManager::primary(32);
        if (bigTitleFont != nullptr) {
            lv_obj_set_style_text_font(s_splashTitle, bigTitleFont, 0);
            lv_obj_align(s_splashTitle, LV_ALIGN_CENTER, 0, -50);
            lv_task_handler();
        }
    }

    // 4. Config
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");
    updateSplash("Applying config...", 55);

    // 5. Runtime
    LOG_INFO("BOOT", "Initializing TimerService...");
    TimerService::init();
    LOG_INFO("BOOT", "Initializing SignalStore...");
    SignalStore::init();
    LOG_INFO("BOOT", "Initializing AlertEngine...");
    AlertEngine::init();
    LOG_INFO("BOOT", "Runtime ready");
    updateSplash("Starting runtime...", 65);

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
    logHeap("dashboard ready");

    updateSplash("Ready", 100);

    // Hold the splash for at least SPLASH_MIN_MS so the user can read the
    // version + final progress state before the dashboard takes over.
    const uint32_t bootElapsed = millis() - bootStartMs;
    if (bootElapsed < SPLASH_MIN_MS) {
        delay(SPLASH_MIN_MS - bootElapsed);
    }

    logHeap("boot complete");
    LOG_INFO("BOOT", "Boot sequence complete (splash held %lu ms)",
             static_cast<unsigned long>(millis() - bootStartMs));

    // CI smoke-test marker — QEMU boot smoke-test gate asserts this exact line
    // appears once within 30 s. Do not remove. See
    // .github/workflows/firmware-boot-smoke.yml (issue #486).
    LOG_INFO("BOOT", "[BOOT] Ready");
}
