// boot_sequence.cpp — Power-on initialization sequence

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
#include "runtime/alert_engine.h"
#include "runtime/timer_service.h"
#include "ui/page_manager.h"
#include "ui/theme_manager.h"
#include "ui/font_manager.h"
#include "ui/top_bar.h"

#if !APP_SIMULATION_MODE
    #include "can/can_manager.h"
#endif

#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#if !APP_SIMULATION_MODE
    #include <esp_ota_ops.h>
    #include <esp_task_wdt.h>
#endif
#include <lvgl.h>

// Diagnostic — log free heap, largest contiguous LVGL-relevant block, and
// the lifetime low-watermark at a named boot stage. Helps pinpoint memory
// pressure without needing a debugger. MALLOC_CAP_INTERNAL is the pool LVGL
// allocates from, so it is the metric that actually tracks UI headroom.
// When PSRAM is present the free-PSRAM byte count is appended so field
// reports show the WROVER headroom win at a glance (issue #563).
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

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void initDisplayAndLVGL() {
    // lv_init() allocates the LVGL pool (LV_MEM_SIZE) via malloc(). It must
    // run BEFORE DisplayDriver::init() because LovyanGFX's s_lcd.init()
    // fragments the heap such that a large contiguous block is no longer
    // available afterwards. lv_init() does not need the display; only
    // registerWithLVGL() does (it calls lv_disp_drv_register).
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

// ---------------------------------------------------------------------------
// Splash screen — title + version + progress bar + status label.
// No banner image (looked off on this small panel) — text-only is enough.
// ---------------------------------------------------------------------------

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
    PageManager::navigateTo(PageManager::getDefaultPageId());

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
// (e.g. factory boot, or already-marked slot from an earlier boot). Called
// once after [BOOT] Ready — the simpler "immediate" criterion: if execution
// reaches this point the UI is built and the splash has rendered, which is a
// strong signal of a healthy image. Tradeoff: a crash within the first few
// hundred ms after this call will NOT trigger rollback. A delayed mark (e.g.
// 10 s into the UI loop) would catch that class of bug at the cost of a
// longer rollback window — out of scope here. Issue #674.
#if !APP_SIMULATION_MODE
static void markOtaSlotValidIfPending() {
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
#endif

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

    // Detect PSRAM before any allocation so the display driver can offload
    // its LVGL draw buffers to SPIRAM when the chip is a WROVER (issue #563).
    canshift::hal::memory::initPsram();

    logHeap("entry");

    // Task Watchdog Timer — initialised here, before any task is spawned in
    // main.cpp::createAllTasks(). Registered tasks (UI/CAN/USB) must call
    // esp_task_wdt_reset() each loop iteration or the panic handler resets
    // the device. Skipped in simulation builds because QEMU has no real WDT
    // and the boot smoke test would otherwise reset before reaching
    // "[BOOT] Ready". Issue #666.
    //
    // The arduino-esp32 SDK pinned here ships the IDF v4 TWDT API
    // (seconds + bool, idempotent — re-init updates the existing config).
#if !APP_SIMULATION_MODE
    constexpr uint32_t WDT_TIMEOUT_S = (TASK_WDT_TIMEOUT_MS + 999U) / 1000U;
    const esp_err_t wdtErr = esp_task_wdt_init(WDT_TIMEOUT_S, /*panic=*/true);
    if (wdtErr != ESP_OK) {
        LOG_ERROR("BOOT", "Task WDT init failed: %d — continuing without WDT",
                  static_cast<int>(wdtErr));
    } else {
        LOG_INFO("BOOT", "Task WDT armed (%u s)", static_cast<unsigned>(WDT_TIMEOUT_S));
    }
#endif

// 0. BLE early init — NimBLE needs ~50 KB contiguous DRAM. After LovyanGFX
//    init the largest free block shrinks to ~16 KB, making BLE impossible.
//    Initializing the stack here, before the display, guarantees the
//    allocation succeeds while the heap is still large and unfragmented.
#if APP_BLE_ENABLED
    BleServer::earlyInit();
    logHeap("after BLE early init");
#endif

    // 1. Storage mount — no LVGL dependency; runs before lv_init() so config
    //    can be parsed while the heap still has a large contiguous block.
    //    LvglFsDriver::init() is intentionally deferred until after lv_init().
    const bool storageOk = initStorage();
    if (storageOk) {
        LOG_INFO("BOOT", "Storage mounted");
    } else {
        LOG_ERROR("BOOT", "Storage mount failed — running with defaults");
        ErrorStore::push(ERROR_SRC_SYSTEM, "MOUNT_FAIL", "Storage offline — config not persisted");
    }
    logHeap("after storage");

    // 2. Provision default configs on a fresh / empty SPIFFS before
    //    loadConfig reads. Writes only when target is missing AND no .bak
    //    exists, or when target is empty. Never overwrites user data.
#if DEFAULT_CONFIG_PROVISION_ENABLED
    if (storageOk) {
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
    }
#endif

    // 3. Config — must run BEFORE lv_init() because ArduinoJSON's stream
    //    parser needs ~20 KB contiguous heap. After lv_init() takes its 80 KB
    //    pool the largest free block drops to ~15 KB causing NoMemory parse
    //    failures. At this point the heap has ~120 KB contiguous — ample.
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");

    // 4. Display + LVGL — lv_init() malloc(80 KB) runs here, after config is
    //    parsed and ArduinoJSON scratch freed, so the pool allocation succeeds.
    initDisplayAndLVGL();

    // 5. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    LOG_INFO("BOOT", "Touch ready");

    // 6. LVGL filesystem driver — needs lv_init() (calls lv_fs_drv_register).
    if (storageOk) {
        LvglFsDriver::init();
    }

    // 7. Provision the 8 Orbitron .bin fonts on a fresh / empty SPIFFS
    //    BEFORE FontManager::init() so lv_font_load() finds them. Writes
    //    only when target is missing — never overwrites existing files.
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

    // 8. Load SPIFFS-backed fonts before showSplash() so the logo renders at
    //    Orbitron Black 32 from the very first frame — no two-phase appearance.
    LOG_INFO("BOOT", "Initializing FontManager...");
    FontManager::init();
    LOG_INFO("BOOT", "FontManager ready");
    logHeap("after FontManager");

    // 9. Show splash — fonts loaded, logo at full size from first frame.
    LOG_INFO("BOOT", "Showing splash...");
    showSplash();
    LOG_INFO("BOOT", "Splash visible");
    logHeap("after splash");

    updateSplash("Config loaded", 10);
    updateSplash("Applying config...", 40);

    // 10. Runtime
    LOG_INFO("BOOT", "Initializing TimerService...");
    TimerService::init();
    LOG_INFO("BOOT", "Initializing SignalStore...");
    SignalStore::init();
    LOG_INFO("BOOT", "Initializing AlertEngine...");
    AlertEngine::init();
    LOG_INFO("BOOT", "Runtime ready");
    updateSplash("Starting runtime...", 60);

    // 11. CAN hardware (skip in simulation mode)
#if !APP_SIMULATION_MODE
    LOG_INFO("BOOT", "Initializing CAN/TWAI...");
    CanManager::initHardware();
    updateSplash("CAN ready", 78);
#else
    LOG_INFO("BOOT", "Simulation mode — skipping CAN init");
    updateSplash("Simulation mode", 78);
#endif

    // 12. USB comm
    LOG_INFO("BOOT", "Initializing USB comm...");
    UsbComm::init();
    updateSplash("USB ready", 90);

    // 13. Build the UI from config.
    // updateSplash("Ready") must happen BEFORE buildUI() because
    // PageManager::init() calls lv_obj_clean(lv_scr_act()) to free the
    // splash objects from the LVGL pool before building page widgets.
    // Any call to updateSplash after that point would dereference freed objects.
    updateSplash("Ready", 100);
    logHeap("before buildUI");
    buildUI();
    logHeap("dashboard ready");

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

    // OTA rollback handshake — confirm this image is healthy so the
    // bootloader cancels its pending rollback. Sim builds have no OTA
    // partitions, so the call is compiled out. Issue #674.
#if !APP_SIMULATION_MODE
    markOtaSlotValidIfPending();
#endif
}
