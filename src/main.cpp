
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
#include "can/can_manager.h"
#include "can/can_task.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "diag/lvgl_hold_timer.h"
#include "diag/perf_counters.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/usb/usb_comm.h"
#include "hal/wifi/workshop_server.h"
#include "runtime/alert_engine.h"
#include "runtime/input_buttons.h"
#include "runtime/lvgl_lock.h"
#include "runtime/pending_actions.h"
#include "runtime/task_spawn.h"
#include "runtime/workshop_mode.h"
#include "ui/burn_overlay.h"
#include "ui/day_night_auto.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"
#include "ui/passkey_overlay.h"
#include "ui/theme_manager.h"
#include "ui/workshop_screen.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
void taskUI(void *pvParameters);
void taskUSBComm(void *pvParameters);
#if APP_BLE_ENABLED
void taskBLE(void *pvParameters);
#endif

SemaphoreHandle_t g_lvglMutex = nullptr;

TaskHandle_t g_uiTaskHandle = nullptr;

static TaskHandle_t s_usbTaskHandle = nullptr;
#if APP_BLE_ENABLED
static TaskHandle_t s_bleTaskHandle = nullptr;
#endif

static StackType_t *s_uiStack = nullptr;
static StackType_t *s_usbStack = nullptr;
#if APP_BLE_ENABLED
static StackType_t *s_bleStack = nullptr;
#endif

static StaticTask_t s_uiTaskTCB;
static StaticTask_t s_usbTaskTCB;
#if APP_BLE_ENABLED
static StaticTask_t s_bleTaskTCB;
#endif

static void preallocateTaskStacks() {
    s_uiStack = static_cast<StackType_t *>(heap_caps_malloc(TASK_STACK_UI * sizeof(StackType_t),
                                                            MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    const bool canStackOk = CanTask::reserveStack();
    s_usbStack = static_cast<StackType_t *>(heap_caps_malloc(
        TASK_STACK_USB * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
#if APP_BLE_ENABLED
    s_bleStack = static_cast<StackType_t *>(heap_caps_malloc(
        TASK_STACK_BLE * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
#endif
    if (!s_uiStack || !canStackOk || !s_usbStack
#if APP_BLE_ENABLED
        || !s_bleStack
#endif
    ) {
        LOG_ERROR("BOOT", "Task stack pre-allocation failed — halting");

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

static void burnOverlayShowObserver() {
    PendingActions::burnOverlayShow.store(true, std::memory_order_relaxed);
    if (g_uiTaskHandle != nullptr) {
        xTaskNotifyGive(g_uiTaskHandle);
    }
}

static void burnOverlayShowErrorObserver(int reason) {
    PendingActions::burnOverlayShowError.store(static_cast<int8_t>(reason),
                                               std::memory_order_relaxed);
    if (g_uiTaskHandle != nullptr) {
        xTaskNotifyGive(g_uiTaskHandle);
    }
}

static void registerBurnOverlayObserver() {
    UsbComm::setBurnOverlayShowCallback(&burnOverlayShowObserver);
    UsbComm::setBurnOverlayShowErrorCallback(&burnOverlayShowErrorObserver);
}

static void createAllTasks() {
    g_uiTaskHandle = TaskSpawn::spawnStatic("ui", taskUI, TASK_STACK_UI, kTaskPrioUi, s_uiStack,
                                            &s_uiTaskTCB, kTaskCoreUi);
    s_usbTaskHandle = TaskSpawn::spawnStatic("usb", taskUSBComm, TASK_STACK_USB, kTaskPrioUsb,
                                             s_usbStack, &s_usbTaskTCB, kTaskCoreUsb);
#if APP_BLE_ENABLED
    s_bleTaskHandle = TaskSpawn::spawnStatic("ble", taskBLE, TASK_STACK_BLE, kTaskPrioBle,
                                             s_bleStack, &s_bleTaskTCB, kTaskCoreBle);
#endif
}

void setup() {
    Serial.setRxBufferSize(2048);
    Serial.setTxBufferSize(kUsbTxBufferBytes);
    Serial.begin(kUsbSerialBaud);
    delay(200);

    Logger::init();
    LOG_INFO("BOOT", "CANShift v" APP_VERSION_STR " starting");

    g_lvglMutex = xSemaphoreCreateMutex();
    if (!g_lvglMutex) {
        LOG_ERROR("BOOT", "Failed to create LVGL mutex — halting");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    preallocateTaskStacks();

    UsbComm::reserveRxBuf();
    CanManager::reserveInitTaskStack();

    BootSequence::run();
    PERF_INIT();

    LOG_INFO("BOOT", "Boot complete — starting tasks");
    createAllTasks();

    registerBurnOverlayObserver();
    InputButtons::init();
    LOG_INFO("BOOT", "All tasks started — releasing the %u B setup stack",
             static_cast<unsigned>(getArduinoLoopTaskStackSize()));
    vTaskDelete(nullptr);
}

void loop() {}

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
    if (passkey != 0u) {
        PasskeyOverlay::show(passkey);
    }
}

inline void uiDrainBurnOverlayActions() {
    if (PendingActions::takeBurnOverlayShow()) {
        BurnOverlay::show();
    }
    const int8_t err = PendingActions::takeBurnOverlayShowError();
    if (err >= 0) {
        BurnOverlay::showError(static_cast<BurnOverlay::ErrorReason>(err));
    }
}

inline void uiDrainConfigReload() {
    if (!PendingActions::takeConfigReload()) {
        return;
    }
    PageManager::reloadFromStorage();
}

inline void uiDrainNavActions() {
    char pageId[kCfgMaxIdLen];
    if (PendingActions::takeNavPage(pageId, sizeof(pageId))) {
        PageManager::navigateTo(pageId);
    }
}

inline void uiDrainOtaOverlayActions() {
    char targetVersion[PendingActions::kOtaVersionLen];
    const uint32_t showSize =
        PendingActions::takeOtaOverlayShow(targetVersion, sizeof(targetVersion));
    if (showSize > 0) {
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
    const bool entered = WorkshopScreen::enter();
    WorkshopMode::settleEntry(entered);
    if (entered)
        uiHandRadioToWorkshop();
}

inline void uiDrainWorkshopConfigReload() {
    if (PendingActions::takeConfigReload())
        BurnOverlay::hide();
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

inline void uiNotifyDayNightChanged(bool didDayNightChange) {
#if APP_BLE_ENABLED
    if (didDayNightChange) {
        BleServer::pushStatusNotify();
    }
#else
    (void)didDayNightChange;
#endif
}

#if APP_PROFILE_UI
constexpr uint32_t kStackLogIntervalMs = 5000;

inline void logStackHeadroom(const char *name, TaskHandle_t task) {
    if (task == nullptr)
        return;
    const UBaseType_t wordsFree = uxTaskGetStackHighWaterMark(task);
    LOG_INFO("PERF", "stack '%s': %u bytes free at worst", name,
             static_cast<unsigned>(wordsFree) * sizeof(StackType_t));
}

inline void uiReportStackHeadroom() {
    static uint32_t lastLogMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastLogMs < kStackLogIntervalMs)
        return;
    lastLogMs = nowMs;
    logStackHeadroom("ui", g_uiTaskHandle);
    logStackHeadroom("can", CanTask::handle());
    logStackHeadroom("usb", s_usbTaskHandle);
    #if APP_BLE_ENABLED
    logStackHeadroom("ble", s_bleTaskHandle);
    #endif
}
#endif

inline void uiRecordFrameMetrics(int64_t frameStartUs, TickType_t lastWake) {
#if APP_PROFILE_UI
    const int64_t frameEndUs = esp_timer_get_time();
    ::PerfCounters::recordSample(::PerfCounters::FRAME_TOTAL,
                                 static_cast<uint32_t>(frameEndUs - frameStartUs));
    const TickType_t nowTicks = xTaskGetTickCount();
    if ((nowTicks - lastWake) > pdMS_TO_TICKS(kLvglHandlerPeriodMs + 2)) {
        ::PerfCounters::recordFrameMiss();
    }
    ::PerfCounters::tick();
    uiReportStackHeadroom();
#else
    (void)frameStartUs;
    (void)lastWake;
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

} // namespace

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

void taskUSBComm(void *pvParameters) {
    UsbComm::init();

#if APP_USB_TICK_TRACE

    int64_t prevEntryUs = esp_timer_get_time();
    uint32_t tickCount = 0;
#endif

    while (true) {
#if APP_USB_TICK_TRACE
        const int64_t entryUs = esp_timer_get_time();
        const int64_t gapUs = entryUs - prevEntryUs;
        if (gapUs > static_cast<int64_t>(kUsbTickIntervalWarnUs)) {
            LOG_WARN("USB_TRACE", "tick gap %lld us (count=%lu) — USB task starved",
                     static_cast<long long>(gapUs), static_cast<unsigned long>(tickCount));
        }
        prevEntryUs = entryUs;
        const int64_t bodyStartUs = entryUs;
#endif

        UsbComm::tick();
        WorkshopServer::tick();

#if APP_USB_TICK_TRACE
        const int64_t bodyDurUs = esp_timer_get_time() - bodyStartUs;
        if (bodyDurUs > static_cast<int64_t>(kUsbTickDurationWarnUs)) {
            LOG_WARN("USB_TRACE", "tick body %lld us (count=%lu) — slow path inside tick()",
                     static_cast<long long>(bodyDurUs), static_cast<unsigned long>(tickCount));
        }
        ++tickCount;
#endif

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(kUsbTaskTickIntervalMs));
    }
}

#if APP_BLE_ENABLED

void taskBLE(void *pvParameters) {
    BleServer::init();

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        if (BleServer::takeStopRequest()) {
            BleServer::stop();
        }
        if (BleServer::takeReleaseRequest()) {
            BleServer::release();
            WorkshopServer::requestStart();
        }
        BleServer::tick();

        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kBleTeleIntervalMs));
    }
}

#endif
