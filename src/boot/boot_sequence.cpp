// boot_sequence.cpp — Power-on initialization sequence

#include "boot_sequence.h"
#include "app_config.h"
#include "board_config.h"

#include "diag/logger.h"
#include "diag/error_store.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/storage/storage_driver.h"
#include "hal/storage/lvgl_fs_driver.h"
#include "hal/usb/usb_comm.h"
#include "config/config_loader.h"
#include "config/default_config.h"
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
#include <esp_log.h>
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
// Splash screen — title + version + progress bar + status label.
// No banner image (looked off on this small panel) — text-only is enough.
// ---------------------------------------------------------------------------

namespace {
static lv_obj_t *s_splashBar = nullptr;
static lv_obj_t *s_splashStatus = nullptr;
} // namespace

// Build the dark splash background.
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
    lv_obj_set_style_text_color(ver, lv_color_hex(0x666666), 0);
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
    lv_label_set_text(status, "Starting...");
    lv_obj_set_style_text_color(status, lv_color_hex(0x666666), 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 52);

    s_splashBar = bar;
    s_splashStatus = status;

    lv_task_handler();
}

// Set when SD failed to mount during boot. Mirrors getSdStatus() so
// boot helpers can read it before getSdStatus() resolves.
static BootSequence::SdStatus s_sdStatus = BootSequence::SdStatus::Ok;

// SD-status badge — small persistent label rendered on lv_layer_top() in
// the top-right corner. Visible across page changes and stays out of the
// way of the dashboard. Created lazily after buildUI() when the SD did
// not mount cleanly. Distinct text + color for "no card" vs "mount fail"
// gives the user a fighting chance at diagnosing without a serial console.
static constexpr int16_t SD_BADGE_X_OFFSET = -4;
static constexpr int16_t SD_BADGE_Y_OFFSET = 4;
// Amber for "no card" — actionable, just insert one.
static constexpr uint32_t SD_BADGE_NO_CARD_BG = 0xCC8800;
// Red for "mount failed" — wiring/firmware issue, needs an investigation.
static constexpr uint32_t SD_BADGE_FAIL_BG = 0xCC3333;
static constexpr uint32_t SD_BADGE_FG = 0xFFFFFF;
static constexpr int16_t SD_BADGE_PAD_X = 4;
static constexpr int16_t SD_BADGE_PAD_Y = 1;

static void showSdBadge(BootSequence::SdStatus status) {
    if (status == BootSequence::SdStatus::Ok)
        return;

    const char *text = status == BootSequence::SdStatus::NoCard ? "NO SD" : "SD ERR";
    const uint32_t bg = status == BootSequence::SdStatus::NoCard ? SD_BADGE_NO_CARD_BG
                                                                 : SD_BADGE_FAIL_BG;

    lv_obj_t *badge = lv_label_create(lv_layer_top());
    lv_label_set_text(badge, text);
    lv_obj_set_style_text_color(badge, lv_color_hex(SD_BADGE_FG), 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(badge, SD_BADGE_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(badge, SD_BADGE_PAD_Y, LV_PART_MAIN);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, SD_BADGE_X_OFFSET, SD_BADGE_Y_OFFSET);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
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

// Maps a StorageDriver::InitStatus to the boot-level SdStatus. The boot
// view of the SD subsystem is intentionally narrower than the storage
// driver's: we only care whether the card is usable and, if not, whether
// the user should be told to insert one or to investigate wiring.
static BootSequence::SdStatus mapStorageStatus(StorageDriver::InitStatus s) {
    switch (s) {
        case StorageDriver::InitStatus::Ok:
            return BootSequence::SdStatus::Ok;
        case StorageDriver::InitStatus::NoCard:
            return BootSequence::SdStatus::NoCard;
        case StorageDriver::InitStatus::MountFailed:
        case StorageDriver::InitStatus::NotInitialized:
        default:
            return BootSequence::SdStatus::MountFailed;
    }
}

// Returns Ok if the storage came up cleanly. On any other status the boot
// continues with built-in defaults — the device stays reachable over USB.
static BootSequence::SdStatus initStorage() {
    LOG_INFO("BOOT", "Initializing SD card...");
    const bool ok = StorageDriver::init();
    const BootSequence::SdStatus status = mapStorageStatus(StorageDriver::getStatus());
    if (!ok) {
        return status;
    }
    LvglFsDriver::init();
    FontManager::init();
    return status;
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

// Minimum splash visibility — boot tends to finish in < 1 s, which feels
// twitchy and gives the user no time to read the version. Hold at least this
// long before handing the screen over to the dashboard.
static constexpr uint32_t SPLASH_MIN_MS = 2000;

void BootSequence::run() {
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
    logHeap("after lv_init");
    showSplash(); // 0 %

    // 2. Touch controller
    LOG_INFO("BOOT", "Initializing touch...");
    TouchDriver::init();
    updateSplash("Initializing touch...", 15);

    // 3. Storage — degrade (don't halt) if SD missing/broken so the studio
    //    can still reach the device over USB and the user can see a default
    //    dashboard.
    updateSplash("Checking SD card...", 20);
    s_sdStatus = initStorage();
    switch (s_sdStatus) {
        case BootSequence::SdStatus::Ok:
            updateSplash("SD ready...", 35);
            break;
        case BootSequence::SdStatus::NoCard:
            LOG_WARN("BOOT", "SD missing — running with defaults; USB still reachable");
            updateSplash("No SD — defaults", 35);
            break;
        case BootSequence::SdStatus::MountFailed:
            LOG_ERROR("BOOT",
                      "SD mount failed — running with defaults; check pinout/wiring");
            updateSplash("SD error — defaults", 35);
            break;
    }

    // 3.5 Provision default configs on a fresh / empty SD before loadConfig
    //     reads. Writes only when target is missing AND no .bak exists, or
    //     when target is empty. Never overwrites user data. On read-only or
    //     full SD the failure is logged and pushed to ErrorStore — boot
    //     continues so the device stays USB-reachable for recovery.
#if DEFAULT_CONFIG_PROVISION_ENABLED
    if (s_sdStatus == BootSequence::SdStatus::Ok) {
        const DefaultConfig::ProvisionResult pr = DefaultConfig::provisionMissingFiles();
        if (pr.written > 0) {
            LOG_INFO("BOOT", "Provisioned %u default config file(s)",
                     static_cast<unsigned>(pr.written));
            updateSplash("Provisioning defaults...", 45);
        }
        if (pr.failed > 0) {
            LOG_WARN("BOOT",
                     "Default-config provision failed for %u file(s) — "
                     "continuing with whatever is on SD",
                     static_cast<unsigned>(pr.failed));
        }
    }
#endif

    // 4. Config
    logHeap("before loadConfig");
    loadConfig();
    logHeap("after loadConfig");
    updateSplash("Applying config...", 55);

    // 5. Runtime
    SignalStore::init();
    AlertEngine::init();
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
    logHeap("after buildUI");

    // Surface the SD state with a small persistent badge once the dashboard
    // has been built. Non-blocking — sits on lv_layer_top() above all pages.
    showSdBadge(s_sdStatus);

    updateSplash("Ready", 100);

    // Hold the splash for at least SPLASH_MIN_MS so the user can read the
    // version + final progress state before the dashboard takes over.
    const uint32_t bootElapsed = millis() - bootStartMs;
    if (bootElapsed < SPLASH_MIN_MS) {
        delay(SPLASH_MIN_MS - bootElapsed);
    }

    LOG_INFO("BOOT", "Boot sequence complete (splash held %lu ms)",
             static_cast<unsigned long>(millis() - bootStartMs));
}

BootSequence::SdStatus BootSequence::getSdStatus() {
    return s_sdStatus;
}

bool BootSequence::isDegradedNoSd() {
    return s_sdStatus != SdStatus::Ok;
}
