
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
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "hal/usb/usb_comm.h"
#include "hal/wifi/workshop_server.h"
#include "runtime/ui_task.h"
#include "runtime/input_buttons.h"
#include "runtime/pending_actions.h"
#include "runtime/sound.h"
#include "runtime/task_spawn.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
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
    Sound::start();
    LOG_INFO("BOOT", "All tasks started — releasing the %u B setup stack",
             static_cast<unsigned>(getArduinoLoopTaskStackSize()));
    vTaskDelete(nullptr);
}

void loop() {}

#if APP_PROFILE_UI
static constexpr uint32_t kStackLogIntervalMs = 5000;

static void logStackHeadroom(const char *name, TaskHandle_t task) {
    if (task == nullptr)
        return;
    const UBaseType_t wordsFree = uxTaskGetStackHighWaterMark(task);
    LOG_INFO("PERF", "stack '%s': %u bytes free at worst", name,
             static_cast<unsigned>(wordsFree) * sizeof(StackType_t));
}

void reportTaskStackHeadroom() {
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
