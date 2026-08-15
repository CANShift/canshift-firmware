#include "boot_sequence.h"
#include "app_config.h"

#include "boot/boot_phases.h"
#include "can/can_manager.h"
#include "config/board_profile_store.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/usb/usb_comm.h"
#include "runtime/alert_engine.h"
#include "runtime/lvgl_lock.h"
#include "runtime/signal_store.h"
#include "runtime/timer_service.h"
#include "runtime/track_store.h"
#include "ui/boot_screens.h"
#include "ui/page_manager.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <lvgl.h>

namespace {

uint32_t s_bootShownMs = 0;
uint32_t s_selfTestShownMs = 0;

constexpr uint32_t kBootHoldMs = 400;
constexpr uint32_t kSelfTestHoldMs = 2000;
constexpr uint32_t kCanQuietMs = 500;

void holdSince(uint32_t sinceMs, uint32_t minMs) {
    const uint32_t elapsed = millis() - sinceMs;
    if (elapsed >= minMs)
        return;
    vTaskDelay(pdMS_TO_TICKS(minMs - elapsed));
}

BootScreens::CheckResult canBusCheck() {
    const bool ok = CanManager::isAvailable() && CanManager::msSinceLastRx() < kCanQuietMs;
    return {ok ? "OK" : "NO FRAMES", ok};
}

void showBootScreen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    BootScreens::buildBoot(scr);
    s_bootShownMs = millis();
    lv_task_handler();
    lv_refr_now(NULL);
}

void showSelfTestScreen(bool storageOk) {
    const CfgDashboard &dashboard = ConfigLoader::getDashboardConfig();
    char pages[16];
    snprintf(pages, sizeof(pages), "%u PAGES", static_cast<unsigned>(dashboard.pageCount));
    const BootScreens::CheckResult results[BootScreens::kCheckCount] = {
        {"OK", true},
        {"OK", true},
        {storageOk ? "OK" : "NO FLASH", storageOk},
        {dashboard.loaded ? pages : "DEFAULTS", dashboard.loaded},
        canBusCheck()};

    holdSince(s_bootShownMs, kBootHoldMs);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    BootScreens::buildSelfTest(scr, results);
    s_selfTestShownMs = millis();
    lv_task_handler();
    lv_refr_now(NULL);
}

void buildUI() {
    // One lock for the whole build: the mutex is non-recursive, and every call
    // below touches lv_* — taking it per call is what left init() unguarded.
    LvglLock lock(portMAX_DELAY);

    LOG_INFO("BOOT", "Applying theme...");
    ThemeManager::apply();
    LOG_INFO("BOOT", "Initializing PageManager...");
    PageManager::init();
    LOG_INFO("BOOT", "Navigating to default page...");
    PageManager::navigateTo(PageManager::getDefaultPageId());

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LOG_INFO("LVGL", "pool: total=%u free=%u frag=%u%% largest=%u",
             static_cast<unsigned>(mon.total_size), static_cast<unsigned>(mon.free_size),
             static_cast<unsigned>(mon.frag_pct), static_cast<unsigned>(mon.free_biggest_size));

    for (uint8_t i = 0; i < 8; i++) {
        lv_tick_inc(20);
        lv_task_handler();
    }
    LOG_INFO("BOOT", "UI ready");
}

void initRuntimeServices() {
    LOG_INFO("BOOT", "Initializing TimerService...");
    TimerService::init();
    LOG_INFO("BOOT", "Initializing SignalStore...");
    SignalStore::init();
    LOG_INFO("BOOT", "Initializing TrackStore...");
    TrackStore::init();
    LOG_INFO("BOOT", "Initializing AlertEngine...");
    AlertEngine::init();
    LOG_INFO("BOOT", "Runtime ready");
}

void initCanHardwarePhase() {
    LOG_INFO("BOOT", "Initializing CAN/TWAI...");
    const esp_err_t err = CanManager::initHardware();
    if (err == ESP_OK)
        return;
    LOG_ERROR("BOOT", "CAN init failed: %s — degraded mode", esp_err_to_name(err));
    char msg[52];
    snprintf(msg, sizeof(msg), "Boot init failed: %s", esp_err_to_name(err));
    ErrorStore::push(ERROR_SRC_CAN, "BOOT_FAIL", msg);
}

void initUsbCommPhase() {
    LOG_INFO("BOOT", "Initializing USB comm...");
    UsbComm::init();
}

void buildUiWithHeapBracket() {
    holdSince(s_selfTestShownMs, kSelfTestHoldMs);
    BootPhases::logHeap("before buildUI");
    buildUI();
    BootPhases::logHeap("dashboard ready");
}

void logBootCompleteAndReady(uint32_t bootStartMs) {
    BootPhases::logHeap("boot complete");
    LOG_INFO("BOOT", "Boot sequence complete (%lu ms)",
             static_cast<unsigned long>(millis() - bootStartMs));
    LOG_INFO("BOOT", "[BOOT] Ready");
}

} // namespace

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

void BootSequence::run() {
    BootPhases::silenceFrameworkLogNoise();
    BoardProfileStore::loadAndApply();
    const uint32_t bootStartMs = millis();
    BootPhases::initPsramAndLogEntry();
    BootPhases::initTaskWatchdog();
    BootPhases::initLvglMemoryPool();

    const bool storageOk = BootPhases::mountStorageOrLogError();
    BootPhases::provisionDefaultConfigsIfNeeded(storageOk);
    // Config parses before BLE claims its ~55 KB of internal heap — the
    // transient JsonDocument for a full 8-page dashboard needs the room on
    // PSRAM-less WROOM modules (#1596 debugging session).
    BootPhases::loadConfigWithHeapBracket();
    ScreenProfile::initFromDashboard();
    BootPhases::initBleEarlyIfEnabled();

    BootPhases::initDisplayHardware();
    BootPhases::initTouchHardware();
    BootPhases::initLvglFsIfStorageOk(storageOk);
    BootPhases::provisionDefaultFontsIfNeeded(storageOk);
    BootPhases::initFontManagerWithHeapLog();
    BootPhases::preloadIconsWithHeapLog();

    showBootScreen();
    BootPhases::logHeap("after boot screen");
    initRuntimeServices();
    initCanHardwarePhase();
    initUsbCommPhase();
    showSelfTestScreen(storageOk);
    buildUiWithHeapBracket();

    logBootCompleteAndReady(bootStartMs);
}
