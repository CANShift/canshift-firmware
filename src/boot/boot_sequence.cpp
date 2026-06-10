#include "boot_sequence.h"
#include "app_config.h"
#include "board_config.h"
#include "hardware_profile.h"

#include "diag/logger.h"
#include "diag/error_store.h"
#include "hal/display/display_driver.h"
#include "hal/memory/psram.h"
#include "hal/touch/touch_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/storage/lvgl_fs_driver.h"
#include "hal/usb/usb_comm.h"
#include "boot/default_fonts.h"
#include "config/config_loader.h"
#include "config/default_config.h"
#include "runtime/signal_store.h"
#include "runtime/track_store.h"
#include "runtime/alert_engine.h"
#include "runtime/timer_service.h"
#include "ui/icon_assets.h"
#include "ui/page_manager.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"
#include "ui/font_manager.h"
#include "ui/top_bar.h"

#include "can/can_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern SemaphoreHandle_t g_lvglMutex;

#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <lvgl.h>

// MALLOC_CAP_INTERNAL is the pool LVGL allocates from — the metric that
// actually tracks UI headroom.
static void logHeap(const char *stage) {
    const uint32_t free = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t minFree = ESP.getMinFreeHeap();
    if (canshift::hal::memory::isPsramAvailable()) {
        const uint32_t freePsram = static_cast<uint32_t>(canshift::hal::memory::getFreePsram());
        LOG_INFO("HEAP", "%s: free=%u largest=%u min=%u psram_free=%u", stage,
                 static_cast<unsigned>(free), static_cast<unsigned>(largest),
                 static_cast<unsigned>(minFree), static_cast<unsigned>(freePsram));
    } else {
        LOG_INFO("HEAP", "%s: free=%u largest=%u min=%u", stage, static_cast<unsigned>(free),
                 static_cast<unsigned>(largest), static_cast<unsigned>(minFree));
    }
}

static void initDisplayAndLVGL() {
    // lv_init() must precede DisplayDriver::init() — LovyanGFX's s_lcd.init()
    // fragments the heap such that LVGL's pool malloc no longer fits.
    LOG_INFO("BOOT", "Calling lv_init()...");
    lv_init();
    LOG_INFO("BOOT", "lv_init() returned");
    logHeap("after lv_init");

    LOG_INFO("BOOT", "Initializing display...");
    DisplayDriver::init();
    LOG_INFO("BOOT", "Display driver up");

    LOG_INFO("BOOT", "Registering display with LVGL...");
    DisplayDriver::registerWithLVGL();
    LOG_INFO("BOOT", "Display registered with LVGL");

    LOG_INFO("BOOT", "Display + LVGL ready");
}

namespace {
static lv_obj_t *s_splashBar = nullptr;
static lv_obj_t *s_splashStatus = nullptr;
} // namespace

// Build the dark splash background.
// FontManager::init() must have run before this is called so the logo renders
// at Orbitron Black 32 from the very first frame (no two-phase appearance).
static lv_obj_t *buildSplashBase() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -50);

    const lv_font_t *titleFont = FontManager::primary(32);

    lv_obj_t *titleCan = lv_label_create(row);
    lv_label_set_text(titleCan, "CAN");
    lv_obj_set_style_text_color(titleCan, lv_color_hex(0x9A9A9A), 0);
    if (titleFont)
        lv_obj_set_style_text_font(titleCan, titleFont, 0);

    lv_obj_t *titleShift = lv_label_create(row);
    lv_label_set_text(titleShift, "Shift");
    lv_obj_set_style_text_color(titleShift, lv_color_hex(0xFF4444), 0);
    if (titleFont)
        lv_obj_set_style_text_font(titleShift, titleFont, 0);

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
// NOTE: Does NOT call LvglFsDriver::init() — that requires lv_init() to have
// run first (lv_fs_drv_register) and is called separately after LVGL is up.
static bool initStorage() {
    return StorageDriver::init();
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
    // navigateTo has an LVGL_ASSERT_LOCKED canary. Boot is single-threaded
    // (taskUI not spawned yet) so no race is possible, but the assert can't
    // distinguish boot from steady-state — hold the mutex briefly to honour
    // the contract.
    xSemaphoreTake(g_lvglMutex, portMAX_DELAY);
    PageManager::navigateTo(PageManager::getDefaultPageId());
    xSemaphoreGive(g_lvglMutex);

    // Log LVGL pool stats — useful to verify fonts + widgets fit in the pool.
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LOG_INFO("LVGL", "pool: total=%u free=%u frag=%u%% largest=%u",
             static_cast<unsigned>(mon.total_size), static_cast<unsigned>(mon.free_size),
             static_cast<unsigned>(mon.frag_pct), static_cast<unsigned>(mon.free_biggest_size));

    // Drive LVGL through the initial page-transition animation so the
    // display shows the dashboard before the UI task takes over. Without
    // this, the "Ready" splash frame persists until lv_task_handler ticks.
    // lv_refr_now() at t=0 of a FADE_IN renders a black frame (new screen
    // is transparent). Ticking past the animation duration completes it.
    // Use lv_task_handler() (the public API) not lv_timer_handler() —
    // lv_task_handler drives the display-refresh path correctly.
    for (uint8_t i = 0; i < 8; i++) {
        lv_tick_inc(20);
        lv_task_handler();
    }
    LOG_INFO("BOOT", "UI ready");
}

// Mark the running OTA slot as valid so the bootloader cancels its pending
// rollback. No-op when the running partition is not in PENDING_VERIFY state
// (factory boot, or already-marked slot from an earlier boot).
//
// Originally fired once from BootSequence::run() right after [BOOT] Ready —
// "if we reached this point the UI is built, so the image is healthy". That
// criterion was too optimistic: a crash inside the first lv_task_handler()
// call (font decode, page rebuild, theme apply) happens AFTER the mark and
// therefore never triggers rollback. F-ME-8 (#1014) moved the call into
// taskUI, gated on N successfully rendered frames (see UI_OTA_VALID_FRAMES
// in main.cpp). Issue #674.
void BootSequence::markOtaSlotValidIfPending() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return;
    }
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        LOG_INFO("OTA", "Marked running app valid — rollback cancelled");
    } else {
        LOG_WARN("OTA", "esp_ota_mark_app_valid_cancel_rollback returned %s", esp_err_to_name(err));
    }
}

// Demote nvs ERROR → WARN so first-boot NOT_FOUND noise is gone but real
// NVS errors stay visible.
static void silenceNvsLogNoise() {
    esp_log_level_set("nvs", ESP_LOG_WARN);
}

// Must precede any allocation so DisplayDriver can offload LVGL draw buffers
// to SPIRAM on WROVER boards.
static void initPsramAndLogEntry() {
    canshift::hal::memory::initPsram();
    logHeap("entry");
}

// Registered tasks must call esp_task_wdt_reset() each loop iteration or the
// panic handler resets the device.
//
// arduino-esp32 ships IDF v4 TWDT API
// (seconds + bool, idempotent — re-init updates the existing config).
static void initTaskWatchdog() {
    constexpr uint32_t WDT_TIMEOUT_S = (TASK_WDT_TIMEOUT_MS + 999U) / 1000U;
    const esp_err_t wdtErr = esp_task_wdt_init(WDT_TIMEOUT_S, /*panic=*/true);
    if (wdtErr != ESP_OK) {
        LOG_ERROR("BOOT", "Task WDT init failed: %d — continuing without WDT",
                  static_cast<int>(wdtErr));
    } else {
        LOG_INFO("BOOT", "Task WDT armed (%u s)", static_cast<unsigned>(WDT_TIMEOUT_S));
    }
}

// BLE early init — NimBLE needs ~50 KB contiguous DRAM. After LovyanGFX
// init the largest free block shrinks to ~16 KB, making BLE impossible.
// Initializing the stack here, before the display, guarantees the
// allocation succeeds while the heap is still large and unfragmented.
static void initBleEarlyIfEnabled() {
#if APP_BLE_ENABLED
    BleServer::earlyInit();
    logHeap("after BLE early init");
#endif
}

// Storage mount — no LVGL dependency; runs before lv_init() so config
// can be parsed while the heap still has a large contiguous block.
// LvglFsDriver::init() is intentionally deferred until after lv_init().
// Returns true if SPIFFS mounted successfully.
static bool mountStorageOrLogError() {
    const bool storageOk = initStorage();
    if (storageOk) {
        LOG_INFO("BOOT", "Storage mounted");
    } else {
        LOG_ERROR("BOOT", "Storage mount failed — running with defaults");
        ErrorStore::push(ERROR_SRC_SYSTEM, "MOUNT_FAIL", "Storage offline — config not persisted");
    }
    logHeap("after storage");
    return storageOk;
}

// Provision default configs on a fresh / empty SPIFFS before
// loadConfig reads. Writes only when target is missing AND no .bak
// exists, or when target is empty. Never overwrites user data.
static void provisionDefaultConfigsIfNeeded(bool storageOk) {
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

// Config — must run BEFORE lv_init() because ArduinoJSON's stream
// parser needs ~20 KB contiguous heap. After lv_init() takes its 80 KB
// pool the largest free block drops to ~15 KB causing NoMemory parse
// failures. At this point the heap has ~120 KB contiguous — ample.
static void loadConfigWithHeapBracket() {
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");
}

static void initTouchHardware() {
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    LOG_INFO("BOOT", "Touch ready");
}

// LVGL filesystem driver — needs lv_init() (calls lv_fs_drv_register).
static void initLvglFsIfStorageOk(bool storageOk) {
    if (storageOk) {
        LvglFsDriver::init();
    }
}

// Provision the 8 Orbitron .bin fonts on a fresh / empty SPIFFS
// BEFORE FontManager::init() so lv_font_load() finds them. Writes
// only when target is missing — never overwrites existing files.
static void provisionDefaultFontsIfNeeded(bool storageOk) {
    if (storageOk) {
        LOG_INFO("BOOT", "Provisioning default fonts (if needed)...");
        const DefaultFonts::ProvisionResult fr = DefaultFonts::provisionMissingFiles();
        if (fr.written > 0)
            LOG_INFO("BOOT", "Provisioned %u default font file(s)",
                     static_cast<unsigned>(fr.written));
        if (fr.failed > 0)
            LOG_WARN("BOOT",
                     "Default-font provision failed for %u file(s) — "
                     "FontManager will fall back to built-in glyph",
                     static_cast<unsigned>(fr.failed));
    }
    logHeap("before FontManager");
}

// Load SPIFFS-backed fonts before showSplash() so the logo renders at the
// active family's primary size from the very first frame — no two-phase
// appearance.
static void initFontManagerWithHeapLog() {
    LOG_INFO("BOOT", "Initializing FontManager...");
    FontManager::init();
    LOG_INFO("BOOT", "FontManager ready");
    logHeap("after FontManager");
}

// Preload dashboard icons + theme icons while the heap still has a
// contiguous block large enough for the SPIFFS reads. Once page
// widgets start allocating against the same pool the largest free
// block drops below the FS-open threshold and on-demand icon
// loads fail (#956). Preloaded entries live in the LVGL image
// cache (LV_IMG_CACHE_DEF_SIZE) and survive page rebuilds + theme
// toggles without re-touching SPIFFS.
static void preloadIconsWithHeapLog() {
    LOG_INFO("BOOT", "Preloading dashboard icons...");
    IconAssets::preloadDashboardAssets();
    logHeap("after icon preload");
}

// Show splash — fonts loaded, logo at full size from first frame.
static void showSplashWithInitialUpdates() {
    LOG_INFO("BOOT", "Showing splash...");
    showSplash();
    LOG_INFO("BOOT", "Splash visible");
    logHeap("after splash");

    updateSplash("Config loaded", 10);
    updateSplash("Applying config...", 40);
}

static void initRuntimeServices() {
    LOG_INFO("BOOT", "Initializing TimerService...");
    TimerService::init();
    LOG_INFO("BOOT", "Initializing SignalStore...");
    SignalStore::init();
    LOG_INFO("BOOT", "Initializing TrackStore...");
    TrackStore::init();
    LOG_INFO("BOOT", "Initializing AlertEngine...");
    AlertEngine::init();
    LOG_INFO("BOOT", "Runtime ready");
    updateSplash("Starting runtime...", 60);
}

static void initCanHardwarePhase() {
    LOG_INFO("BOOT", "Initializing CAN/TWAI...");
    // Capture init result (#1224). Boot continues even on failure so the UI
    // and USB remain usable for config edits; the in-driver retry loop in
    // can_manager.cpp::tick() will keep re-attempting installation. We
    // surface the failure through ErrorStore so the top bar / diag drawer
    // can show it instead of silently reading 0 fps forever.
    const esp_err_t err = CanManager::initHardware();
    if (err != ESP_OK) {
        LOG_ERROR("BOOT", "CAN init failed: %s — degraded mode", esp_err_to_name(err));
        char msg[52];
        snprintf(msg, sizeof(msg), "Boot init failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "BOOT_FAIL", msg);
        updateSplash("CAN unavailable", 78);
    } else {
        updateSplash("CAN ready", 78);
    }
}

static void initUsbCommPhase() {
    LOG_INFO("BOOT", "Initializing USB comm...");
    UsbComm::init();
    updateSplash("USB ready", 90);
}

// Build the UI from config.
// updateSplash("Ready") must happen BEFORE buildUI() because
// PageManager::init() calls lv_obj_clean(lv_scr_act()) to free the
// splash objects from the LVGL pool before building page widgets.
// Any call to updateSplash after that point would dereference freed objects.
static void buildUiWithHeapBracket() {
    updateSplash("Ready", 100);
    logHeap("before buildUI");
    buildUI();
    logHeap("dashboard ready");
}

// Minimum splash visibility — boot tends to finish in < 1 s, which feels
// twitchy and gives the user no time to read the version. Hold at least this
// long before handing the screen over to the dashboard.
static constexpr uint32_t SPLASH_MIN_MS = 2000;

// Splash wait quantum — short enough that the IDLE task / WDT / lv_timer_handler
// (via tick interrupt) keep ticking through the hold (#1207).
static constexpr uint32_t SPLASH_WAIT_STEP_MS = 50;

// Hold the splash for at least SPLASH_MIN_MS so the user can read the
// version + final progress state before the dashboard takes over.
// Uses an explicit yielding vTaskDelay loop (rather than the Arduino
// delay() shim) so the scheduler keeps running other tasks during the
// hold and the granularity is visible to readers (#1207).
static void holdSplashUntilMin(uint32_t bootStartMs) {
    while (true) {
        const uint32_t bootElapsed = millis() - bootStartMs;
        if (bootElapsed >= SPLASH_MIN_MS)
            break;
        const uint32_t remaining = SPLASH_MIN_MS - bootElapsed;
        const uint32_t step = remaining < SPLASH_WAIT_STEP_MS ? remaining : SPLASH_WAIT_STEP_MS;
        vTaskDelay(pdMS_TO_TICKS(step));
    }
}

// Final boot logs + CI smoke-test marker.
// The "[BOOT] Ready" line is asserted by .github/workflows/firmware-boot-smoke.yml
// (issue #486) — it must appear exactly once at the end of a successful boot.
static void logBootCompleteAndReady(uint32_t bootStartMs) {
    logHeap("boot complete");
    LOG_INFO("BOOT", "Boot sequence complete (splash held %lu ms)",
             static_cast<unsigned long>(millis() - bootStartMs));
    LOG_INFO("BOOT", "[BOOT] Ready");
}

void BootSequence::run() {
    silenceNvsLogNoise();
    const uint32_t bootStartMs = millis();
    initPsramAndLogEntry();
    initTaskWatchdog();
    initBleEarlyIfEnabled();

    const bool storageOk = mountStorageOrLogError();
    provisionDefaultConfigsIfNeeded(storageOk);
    loadConfigWithHeapBracket();
    ScreenProfile::initFromDashboard();

    initDisplayAndLVGL();
    initTouchHardware();
    initLvglFsIfStorageOk(storageOk);
    provisionDefaultFontsIfNeeded(storageOk);
    initFontManagerWithHeapLog();
    preloadIconsWithHeapLog();

    showSplashWithInitialUpdates();
    initRuntimeServices();
    initCanHardwarePhase();
    initUsbCommPhase();
    buildUiWithHeapBracket();

    holdSplashUntilMin(bootStartMs);
    logBootCompleteAndReady(bootStartMs);
}
