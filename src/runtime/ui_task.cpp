
#include <Arduino.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "app_config.h"
#include "board_config.h"
#include "hardware_profile.h"

#include <esp_task_wdt.h>

#include "boot/boot_sequence.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/lvgl_hold_timer.h"
#include "diag/perf_counters.h"
#include "hal/touch/touch_driver.h"
#include "hal/wifi/workshop_server.h"
#include "runtime/ui_task.h"
#include "runtime/lvgl_lock.h"
#include "runtime/config_reload_status.h"
#include "runtime/pending_actions.h"
#include "runtime/workshop_mode.h"
#include "ui/alert_takeover.h"
#include "ui/burn_overlay.h"
#include "ui/day_night_auto.h"
#include "ui/display_idle.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"
#include "ui/passkey_overlay.h"
#include "ui/theme_manager.h"
#include "ui/workshop_screen.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif

#if APP_LV_TASK_LOG

static uint32_t s_lvSumUs = 0, s_lvMaxUs = 0, s_lvCount = 0;
static int64_t s_lvLastFlushUs = 0;
#endif

static constexpr int kUiOtaValidFrames = 30;

namespace {

inline void uiDrainPreMutexActions() {
    DayNightAuto::tick();
    if (PendingActions::takeTouchCalibrate()) {
        TouchDriver::calibrate();
#if APP_BLE_ENABLED
        BleServer::pushStatusNotify();
#endif
    }
    if (PendingActions::takeTouchCalibrationReset()) {
        TouchDriver::resetCalibration();
    }
}

inline bool uiDrainDayNightActions() {
    const bool prevIsDay = ThemeManager::isDayMode();

    const int8_t dnSet = PendingActions::takeDayNightSet();
    if (dnSet >= 0) {
        ThemeManager::setDayMode(dnSet == 1);
        (void)PendingActions::takeDayNightToggle();
    } else if (PendingActions::takeDayNightToggle()) {
        ThemeManager::toggleDayMode();
    }

    return ThemeManager::isDayMode() != prevIsDay;
}

inline void uiDrainPasskeyActions() {
    if (PendingActions::takeBlePasskeyHide()) {
        PasskeyOverlay::hide();
    }
    const uint32_t passkey = PendingActions::takeBlePasskeyShow();
    const bool takeoverActive = AlertTakeover::isActive();
    if (passkey != 0u) {
        DisplayIdle::wake();
        PasskeyOverlay::request(passkey);
        if (takeoverActive)
            LOG_WARN("BLE", "Passkey card held — a critical alert owns the screen");
    }
    PasskeyOverlay::update(takeoverActive);
}

inline void uiDrainBurnOverlayActions() {
    if (PendingActions::takeBurnOverlayShow()) {
        DisplayIdle::wake();
        BurnOverlay::show();
    }
    const int8_t err = PendingActions::takeBurnOverlayShowError();
    if (err >= 0) {
        BurnOverlay::showError(static_cast<BurnOverlay::ErrorReason>(err));
    }
}

inline void uiSettleConfigReload(ConfigLoader::DashboardOutcome outcome) {
    ConfigReloadStatus::settle(outcome);
    if (ConfigReloadStatus::refused())
        BurnOverlay::showError(BurnOverlay::ErrorReason::Refused);
}

inline void uiDrainConfigReload() {
    if (!PendingActions::takeConfigReload()) {
        return;
    }
    uiSettleConfigReload(PageManager::reloadFromStorage());
}

inline void uiDrainNavActions() {
    char pageId[kCfgMaxIdLen];
    if (PendingActions::takeNavPage(pageId, sizeof(pageId))) {
        DisplayIdle::wake();
        PageManager::navigateTo(pageId);
    }
}

inline void uiDrainOtaOverlayActions() {
    char targetVersion[PendingActions::kOtaVersionLen];
    const uint32_t showSize =
        PendingActions::takeOtaOverlayShow(targetVersion, sizeof(targetVersion));
    if (showSize > 0) {
        DisplayIdle::wake();
        OtaOverlay::show(showSize, targetVersion);
    }
    if (PendingActions::takeOtaOverlayComplete()) {
        OtaOverlay::showComplete();
    }
    uint32_t failDetail = 0;
    const int8_t failReason = PendingActions::takeOtaOverlayFailReason(&failDetail);
    if (failReason >= 0) {
        OtaOverlay::showFailed(static_cast<OtaOverlay::FailReason>(failReason), failDetail);
    }
    OtaOverlay::Detail::tick();
}

inline void uiHandRadioToWorkshop() {
#if APP_BLE_ENABLED
    BleServer::requestRelease();
#else
    WorkshopServer::requestStart();
#endif
}

inline void uiDrainWorkshopEntry() {
    if (!WorkshopMode::isEntering())
        return;
    DisplayIdle::wake();
    const bool entered = WorkshopScreen::enter();
    WorkshopMode::settleEntry(entered);
    if (entered)
        uiHandRadioToWorkshop();
}

inline void uiDrainWorkshopConfigReload() {
    if (!PendingActions::takeConfigReload())
        return;
    BurnOverlay::hide();
    uiSettleConfigReload(ConfigLoader::vetDashboard());
}

inline void uiRunLvTaskHandler() {
    PERF_SCOPE(::PerfCounters::LV_HANDLER);
#if APP_LV_TASK_LOG
    const int64_t _t0 = esp_timer_get_time();
#endif
    lv_task_handler();
#if APP_LV_TASK_LOG
    const uint32_t _dt = static_cast<uint32_t>(esp_timer_get_time() - _t0);
    s_lvSumUs += _dt;
    if (_dt > s_lvMaxUs)
        s_lvMaxUs = _dt;
    ++s_lvCount;
#endif
}

constexpr uint32_t kUiLvglLockTimeoutMs = 10;

inline void uiRunWorkshopFrame() {
    WorkshopMode::leaveOnMotion(millis());
    uiDrainBurnOverlayActions();
    uiDrainOtaOverlayActions();
    uiDrainWorkshopConfigReload();
    uiRunLvTaskHandler();
}

inline void uiRunDashFrame(bool &didDayNightChange) {
    didDayNightChange = uiDrainDayNightActions();
    uiDrainPasskeyActions();
    uiDrainBurnOverlayActions();
    uiDrainOtaOverlayActions();
    uiDrainConfigReload();
    uiDrainNavActions();
    PageManager::updateWidgets();
    PageManager::publishPages();
    uiRunLvTaskHandler();
}

inline bool uiRunMutexFrame(bool &didDayNightChange) {
#if APP_PROFILE_UI
    const int64_t lockStartUs = esp_timer_get_time();
#endif
    LvglLock lock(pdMS_TO_TICKS(kUiLvglLockTimeoutMs));
#if APP_PROFILE_UI
    ::PerfCounters::recordSample(::PerfCounters::MUTEX_WAIT,
                                 static_cast<uint32_t>(esp_timer_get_time() - lockStartUs));
#endif
    if (!lock.held()) {
        LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        return false;
    }
    LVGL_HOLD_TIMER(::PerfCounters::MUTEX_HOLD_UI);
    uiDrainWorkshopEntry();
    PageManager::tickAlerts();
    if (WorkshopMode::isActive()) {
        uiRunWorkshopFrame();
        return true;
    }
    uiRunDashFrame(didDayNightChange);
    return true;
}

inline void uiHandleOtaMark(int &successfulFrames, bool &otaSlotMarked) {
    if (successfulFrames < kUiOtaValidFrames) {
        ++successfulFrames;
    }
    if (!otaSlotMarked && successfulFrames >= kUiOtaValidFrames) {
        BootSequence::markOtaSlotValidIfPending();
        otaSlotMarked = true;
    }
}

inline void uiFeedTaskWdt() {
    esp_task_wdt_reset();
}

inline void uiNotifyDayNightChanged([[maybe_unused]] bool didDayNightChange) {
#if APP_BLE_ENABLED
    if (didDayNightChange) {
        BleServer::pushStatusNotify();
    }
#endif
}

inline void uiRecordFrameMetrics([[maybe_unused]] int64_t frameStartUs,
                                 [[maybe_unused]] TickType_t lastWake) {
#if APP_PROFILE_UI
    const int64_t frameEndUs = esp_timer_get_time();
    ::PerfCounters::recordSample(::PerfCounters::FRAME_TOTAL,
                                 static_cast<uint32_t>(frameEndUs - frameStartUs));
    const TickType_t nowTicks = xTaskGetTickCount();
    if ((nowTicks - lastWake) > pdMS_TO_TICKS(kLvglHandlerPeriodMs + 2)) {
        ::PerfCounters::recordFrameMiss();
    }
    ::PerfCounters::tick();
    reportTaskStackHeadroom();
#endif
}

inline void uiFlushLvTaskLog() {
#if APP_LV_TASK_LOG
    const int64_t _now = esp_timer_get_time();
    if (s_lvLastFlushUs == 0)
        s_lvLastFlushUs = _now;
    if (_now - s_lvLastFlushUs >= 1000000) {
        const uint32_t avg = s_lvCount ? (s_lvSumUs / s_lvCount) : 0;
        LOG_INFO("PERF", "lv_task: avg=%uus max=%uus n=%u", avg, static_cast<unsigned>(s_lvMaxUs),
                 static_cast<unsigned>(s_lvCount));
        s_lvSumUs = 0;
        s_lvMaxUs = 0;
        s_lvCount = 0;
        s_lvLastFlushUs = _now;
    }
#endif
}

inline void uiThrottle(TickType_t &lastWake) {
    const TickType_t period = pdMS_TO_TICKS(kLvglHandlerPeriodMs);
    const TickType_t now = xTaskGetTickCount();
    const TickType_t elapsed = now - lastWake;
    if (elapsed >= period) {
        vTaskDelay(1);
    } else {
        (void)ulTaskNotifyTake(pdTRUE, period - elapsed);
    }
    lastWake = xTaskGetTickCount();
}

}

void taskUI(void *pvParameters) {
    TickType_t lastWake = xTaskGetTickCount();

    int successfulFrames = 0;
    bool otaSlotMarked = false;

    while (true) {
#if APP_PROFILE_UI
        const int64_t frameStartUs = esp_timer_get_time();
#else
        const int64_t frameStartUs = 0;
#endif
        const TickType_t frameWakeStart = lastWake;

        uiDrainPreMutexActions();

        bool didDayNightChange = false;
        if (uiRunMutexFrame(didDayNightChange)) {
            uiHandleOtaMark(successfulFrames, otaSlotMarked);
        }

        uiFeedTaskWdt();
        uiNotifyDayNightChanged(didDayNightChange);
        uiRecordFrameMetrics(frameStartUs, frameWakeStart);
        uiFlushLvTaskLog();
        uiThrottle(lastWake);
    }
}
