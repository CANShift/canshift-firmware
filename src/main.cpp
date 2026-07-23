
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
#include "diag/logger.h"
#include "diag/lvgl_lock_guard.h"
#include "diag/perf_counters.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/usb/usb_comm.h"
#include "runtime/alert_engine.h"
#include "runtime/input_buttons.h"
#include "runtime/pending_actions.h"
#include "ui/burn_overlay.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"
#include "ui/passkey_overlay.h"
#include "ui/theme_manager.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
void taskUI(void *pvParameters);
void taskCAN(void *pvParameters);
void taskUSBComm(void *pvParameters);
#if APP_BLE_ENABLED
void taskBLE(void *pvParameters);
#endif

SemaphoreHandle_t g_lvglMutex = nullptr;

TaskHandle_t g_uiTaskHandle = nullptr;

static StackType_t *s_uiStack = nullptr;
static StackType_t *s_canStack = nullptr;
static StackType_t *s_usbStack = nullptr;
#if APP_BLE_ENABLED
static StackType_t *s_bleStack = nullptr;
#endif

static StaticTask_t s_uiTaskTCB;
static StaticTask_t s_canTaskTCB;
static StaticTask_t s_usbTaskTCB;
#if APP_BLE_ENABLED
static StaticTask_t s_bleTaskTCB;
#endif

static esp_timer_handle_t s_lvglTickTimer = nullptr;

static void lvglTickCb(void *) {
    lv_tick_inc(LVGL_TICK_MS);
}

static void startLvglTickTimer() {
    const esp_timer_create_args_t args = {
        .callback = &lvglTickCb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_lvglTickTimer));
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(s_lvglTickTimer, static_cast<uint64_t>(LVGL_TICK_MS) * 1000ULL));
}

static void preallocateTaskStacks() {
    s_uiStack = static_cast<StackType_t *>(heap_caps_malloc(TASK_STACK_UI * sizeof(StackType_t),
                                                            MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    s_canStack = static_cast<StackType_t *>(heap_caps_malloc(
        TASK_STACK_CAN * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    s_usbStack = static_cast<StackType_t *>(heap_caps_malloc(
        TASK_STACK_USB * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
#if APP_BLE_ENABLED
    s_bleStack = static_cast<StackType_t *>(heap_caps_malloc(
        TASK_STACK_BLE * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
#endif
    if (!s_uiStack || !s_canStack || !s_usbStack
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
    TaskHandle_t uiHandle = xTaskCreateStaticPinnedToCore(
        taskUI, "ui", TASK_STACK_UI, nullptr, TASK_PRIO_UI, s_uiStack, &s_uiTaskTCB, TASK_CORE_UI);
    g_uiTaskHandle = uiHandle;

    TaskHandle_t canHandle =
        xTaskCreateStaticPinnedToCore(taskCAN, "can", TASK_STACK_CAN, nullptr, TASK_PRIO_CAN,
                                      s_canStack, &s_canTaskTCB, TASK_CORE_CAN);

    TaskHandle_t usbHandle =
        xTaskCreateStaticPinnedToCore(taskUSBComm, "usb", TASK_STACK_USB, nullptr, TASK_PRIO_USB,
                                      s_usbStack, &s_usbTaskTCB, TASK_CORE_USB);

#if APP_BLE_ENABLED
    TaskHandle_t bleHandle =
        xTaskCreateStaticPinnedToCore(taskBLE, "ble", TASK_STACK_BLE, nullptr, TASK_PRIO_BLE,
                                      s_bleStack, &s_bleTaskTCB, TASK_CORE_BLE);
#endif

    if (uiHandle) {
        const esp_err_t err = esp_task_wdt_add(uiHandle);
        if (err != ESP_OK)
            LOG_WARN("BOOT", "WDT add(ui) failed: %d", static_cast<int>(err));
    }
    if (canHandle) {
        const esp_err_t err = esp_task_wdt_add(canHandle);
        if (err != ESP_OK)
            LOG_WARN("BOOT", "WDT add(can) failed: %d", static_cast<int>(err));
    }
    if (usbHandle) {
        const esp_err_t err = esp_task_wdt_add(usbHandle);
        if (err != ESP_OK)
            LOG_WARN("BOOT", "WDT add(usb) failed: %d", static_cast<int>(err));
    }
#if APP_BLE_ENABLED

    if (bleHandle) {
        const esp_err_t err = esp_task_wdt_add(bleHandle);
        if (err != ESP_OK)
            LOG_WARN("BOOT", "WDT add(ble) failed: %d", static_cast<int>(err));
    }
#endif
}

void setup() {
    Serial.setRxBufferSize(2048);
    Serial.begin(USB_SERIAL_BAUD);
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
    startLvglTickTimer();
    PERF_INIT();

    LOG_INFO("BOOT", "Boot complete — starting tasks");
    createAllTasks();

    registerBurnOverlayObserver();
    InputButtons::init();
    LOG_INFO("BOOT", "All tasks started");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#if APP_LV_TASK_LOG

static uint32_t s_lvSumUs = 0, s_lvMaxUs = 0, s_lvCount = 0;
static int64_t s_lvLastFlushUs = 0;
#endif

static constexpr int UI_OTA_VALID_FRAMES = 30;

namespace {

inline void uiDrainPreMutexActions() {
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

inline BaseType_t uiAcquireLvglMutex() {
#if APP_PROFILE_UI
    const int64_t lockStartUs = esp_timer_get_time();
#endif
    const BaseType_t mutexTaken = xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(10));
#if APP_PROFILE_UI
    ::PerfCounters::recordSample(::PerfCounters::MUTEX_WAIT,
                                 static_cast<uint32_t>(esp_timer_get_time() - lockStartUs));
#endif
    return mutexTaken;
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

inline void uiDrainNavActions() {
    char pageId[CFG_MAX_ID_LEN];
    if (PendingActions::takeNavPage(pageId, sizeof(pageId))) {
        PageManager::navigateTo(pageId);
    }
}

inline void uiDrainOtaOverlayActions() {
    const uint32_t showSize = PendingActions::takeOtaOverlayShowSize();
    if (showSize > 0) {
        OtaOverlay::show(showSize);
    }
    if (PendingActions::takeOtaOverlayHide()) {
        OtaOverlay::hide();
    }
    OtaOverlay::Detail::tick();
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

inline bool uiRunMutexBody() {
    LVGL_HOLD_GUARD(::PerfCounters::MUTEX_HOLD_UI);
    const bool didDayNightChange = uiDrainDayNightActions();
    uiDrainPasskeyActions();
    uiDrainBurnOverlayActions();
    uiDrainOtaOverlayActions();
    uiDrainNavActions();
    PageManager::updateWidgets();
    uiRunLvTaskHandler();
    xSemaphoreGive(g_lvglMutex);
    return didDayNightChange;
}

inline void uiHandleOtaMark(int &successfulFrames, bool &otaSlotMarked) {
    if (successfulFrames < UI_OTA_VALID_FRAMES) {
        ++successfulFrames;
    }
    if (!otaSlotMarked && successfulFrames >= UI_OTA_VALID_FRAMES) {
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

inline void uiRecordFrameMetrics(int64_t frameStartUs, TickType_t lastWake) {
#if APP_PROFILE_UI
    const int64_t frameEndUs = esp_timer_get_time();
    ::PerfCounters::recordSample(::PerfCounters::FRAME_TOTAL,
                                 static_cast<uint32_t>(frameEndUs - frameStartUs));
    const TickType_t nowTicks = xTaskGetTickCount();
    if ((nowTicks - lastWake) > pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS + 2)) {
        ::PerfCounters::recordFrameMiss();
    }
    ::PerfCounters::tick();
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
    const TickType_t period = pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS);
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
        if (uiAcquireLvglMutex() != pdTRUE) {
            LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        } else {
            didDayNightChange = uiRunMutexBody();
            uiHandleOtaMark(successfulFrames, otaSlotMarked);
        }

        uiFeedTaskWdt();
        uiNotifyDayNightChanged(didDayNightChange);
        uiRecordFrameMetrics(frameStartUs, frameWakeStart);
        uiFlushLvTaskLog();
        uiThrottle(lastWake);
    }
}

void taskCAN(void *pvParameters) {

    constexpr uint32_t CAN_BURST_BEFORE_YIELD = 16;
    uint32_t consecutiveFrames = 0;
    while (true) {
        const bool frameConsumed = CanManager::tick();

        esp_task_wdt_reset();
        if (!frameConsumed) {
            consecutiveFrames = 0;
            continue;
        }
        if (++consecutiveFrames >= CAN_BURST_BEFORE_YIELD) {
            consecutiveFrames = 0;
            vTaskDelay(CAN_TASK_YIELD_TICKS);
        }
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
        if (gapUs > static_cast<int64_t>(USB_TICK_INTERVAL_WARN_US)) {
            LOG_WARN("USB_TRACE", "tick gap %lld us (count=%lu) — USB task starved",
                     static_cast<long long>(gapUs), static_cast<unsigned long>(tickCount));
        }
        prevEntryUs = entryUs;
        const int64_t bodyStartUs = entryUs;
#endif

        UsbComm::tick();

#if APP_USB_TICK_TRACE
        const int64_t bodyDurUs = esp_timer_get_time() - bodyStartUs;
        if (bodyDurUs > static_cast<int64_t>(USB_TICK_DURATION_WARN_US)) {
            LOG_WARN("USB_TRACE", "tick body %lld us (count=%lu) — slow path inside tick()",
                     static_cast<long long>(bodyDurUs), static_cast<unsigned long>(tickCount));
        }
        ++tickCount;
#endif

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(USB_TASK_TICK_INTERVAL_MS));
    }
}

#if APP_BLE_ENABLED

void taskBLE(void *pvParameters) {
    BleServer::init();

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        const int8_t pending = BleServer::takePendingEnabled();
        if (pending == 0) {
            BleServer::stop();
        } else if (pending == 1) {
            BleServer::start();
        }
        BleServer::tick();

        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BLE_TELE_INTERVAL_MS));
    }
}

#endif
