// main.cpp — Firmware entry point
// Boot sequence, FreeRTOS task creation, and main loop.

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

#if !APP_SIMULATION_MODE
    #include <esp_task_wdt.h>
#endif

#include "boot/boot_sequence.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/usb/usb_comm.h"
#include "runtime/alert_engine.h"
#include "runtime/input_buttons.h"
#include "ui/page_manager.h"
#include "ui/theme_manager.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
#if APP_SIMULATION_MODE
    #include "sim/sim_engine.h"
#endif

// Task function forward declarations
void taskUI(void *pvParameters);
void taskCAN(void *pvParameters);
void taskUSBComm(void *pvParameters);
#if APP_BLE_ENABLED
void taskBLE(void *pvParameters);
#endif
#if APP_SIMULATION_MODE
void taskSim(void *pvParameters);
#endif

// ---------------------------------------------------------------------------
// Global LVGL mutex
// LVGL is NOT thread-safe. All LVGL calls from any task must hold this mutex.
// ---------------------------------------------------------------------------
SemaphoreHandle_t g_lvglMutex = nullptr;

// ---------------------------------------------------------------------------
// Task stack buffers — heap-allocated early in setup() before lv_init() claims
// ~82 KB and fragments DRAM. By task-creation time the largest contiguous block
// drops to ~15 KB, which is smaller than TASK_STACK_UI=8192 + FreeRTOS overhead,
// so xTaskCreatePinnedToCore fails. Pre-allocating here guarantees the blocks
// are reserved from the unfragmented boot heap (~160 KB free).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// LVGL tick — driven by a periodic esp_timer at LVGL_TICK_MS resolution so
// animations and timeouts stay wall-clock accurate even when the UI task
// overruns (page rebuild, theme toggle, slow flush). lv_tick_inc() is the
// only LVGL API documented as safe to call without holding the LVGL mutex,
// so the callback does not take g_lvglMutex.
// ---------------------------------------------------------------------------
static esp_timer_handle_t s_lvglTickTimer = nullptr;

static void lvglTickCb(void * /*arg*/) {
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

// ---------------------------------------------------------------------------
// Allocate task stacks before lv_init() fragments the heap.
// Draw buffers are NOT pre-allocated here — doing so would fragment the heap
// enough that lv_init()'s 80 KB pool malloc returns NULL (→ panic).
// DisplayDriver::init() falls back to single-buffer mode if the second 12 KB
// allocation fails; the flush callback is synchronous so there is no penalty.
// ---------------------------------------------------------------------------
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
            delay(1000);
        }
    }
}

// ---------------------------------------------------------------------------
// Create all FreeRTOS tasks using the pre-allocated static stacks and TCBs.
// xTaskCreateStaticPinnedToCore never allocates from the heap, so it always
// succeeds here even with the fragmented post-lv_init() heap.
//
// Each "hot loop" task (UI/CAN/USB) is also subscribed to the Task Watchdog
// Timer that was armed in BootSequence::run(). Its tick body calls
// esp_task_wdt_reset() once per iteration — a hang in any of those loops
// longer than TASK_WDT_TIMEOUT_MS fires the panic handler and the device
// auto-resets (issue #666). BLE / Sim / WiFi-AP are intentionally NOT
// registered.
// ---------------------------------------------------------------------------
static void createAllTasks() {
    TaskHandle_t uiHandle = xTaskCreateStaticPinnedToCore(
        taskUI, "ui", TASK_STACK_UI, nullptr, TASK_PRIO_UI, s_uiStack, &s_uiTaskTCB, TASK_CORE_UI);

#if !APP_SIMULATION_MODE
    TaskHandle_t canHandle =
        xTaskCreateStaticPinnedToCore(taskCAN, "can", TASK_STACK_CAN, nullptr, TASK_PRIO_CAN,
                                      s_canStack, &s_canTaskTCB, TASK_CORE_CAN);
#else
    xTaskCreatePinnedToCore(taskSim, "sim", TASK_STACK_SIM, nullptr, TASK_PRIO_SIM, nullptr,
                            TASK_CORE_SIM);
#endif

    TaskHandle_t usbHandle =
        xTaskCreateStaticPinnedToCore(taskUSBComm, "usb", TASK_STACK_USB, nullptr, TASK_PRIO_USB,
                                      s_usbStack, &s_usbTaskTCB, TASK_CORE_USB);

#if APP_BLE_ENABLED
    xTaskCreateStaticPinnedToCore(taskBLE, "ble", TASK_STACK_BLE, nullptr, TASK_PRIO_BLE,
                                  s_bleStack, &s_bleTaskTCB, TASK_CORE_BLE);
#endif

#if !APP_SIMULATION_MODE
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
#else
    (void)uiHandle;
    (void)usbHandle;
#endif
}

// ---------------------------------------------------------------------------
// setup() — runs once on core 1 after reset
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(USB_SERIAL_BAUD);
    delay(200); // let serial monitor connect

    Logger::init();
    LOG_INFO("BOOT", "CANShift v" APP_VERSION_STR " starting");

#if APP_SIMULATION_MODE
    LOG_WARN("BOOT", "*** SIMULATION MODE ACTIVE — no CAN hardware ***");
#endif

    g_lvglMutex = xSemaphoreCreateMutex();
    if (!g_lvglMutex) {
        LOG_ERROR("BOOT", "Failed to create LVGL mutex — halting");
        while (true) {
            delay(1000);
        }
    }

    // Must run before BootSequence::run() — see preallocateTaskStacks().
    preallocateTaskStacks();

    // Runs synchronous boot: HAL init → lv_init() → load config → build UI.
    BootSequence::run();

    // Start tick timer only after lv_init() has run inside BootSequence.
    startLvglTickTimer();

    // No-op when APP_PROFILE_UI=0.
    PERF_INIT();

    LOG_INFO("BOOT", "Boot complete — starting tasks");
    createAllTasks();
    // Physical GPIO buttons (#833) — owns its own task, started here so that
    // it sees the configs already loaded by BootSequence::run().
    InputButtons::init();
    LOG_INFO("BOOT", "All tasks started");
}

// ---------------------------------------------------------------------------
// loop() — Arduino main loop. All work is in FreeRTOS tasks.
// ---------------------------------------------------------------------------
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ---------------------------------------------------------------------------
// UI task
// ---------------------------------------------------------------------------

#if APP_LV_TASK_LOG
// 1 Hz aggregator for lv_task_handler() duration. Single-task (taskUI) so no
// synchronization is needed.
static uint32_t s_lvSumUs = 0, s_lvMaxUs = 0, s_lvCount = 0;
static int64_t s_lvLastFlushUs = 0;
#endif

void taskUI(void *pvParameters) {
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
#if APP_PROFILE_UI
        const int64_t frameStartUs = esp_timer_get_time();
#endif

        // Calibration runs WITHOUT the LVGL mutex — it blocks while the user taps
        // crosshairs on screen and draws directly via TFT_eSPI (not through LVGL).
        bool calibratedThisTick = false;
#if APP_BLE_ENABLED
        if (BleServer::takePendingCalibration()) {
            TouchDriver::calibrate();
            BleServer::pushStatusNotify();
            calibratedThisTick = true;
        }
        // Reset-calibration only touches NVS — no LVGL mutex needed, but
        // running it on the UI task keeps all NVS writes on a single thread.
        if (BleServer::takePendingCalibrationReset()) {
            TouchDriver::resetCalibration();
        }
#endif
        if (UsbComm::takePendingCalibration()) {
            TouchDriver::calibrate();
            calibratedThisTick = true;
        }
        if (UsbComm::takePendingCalibrationReset()) {
            TouchDriver::resetCalibration();
        }
        (void)calibratedThisTick;

        bool didDayNightChange = false;

#if APP_PROFILE_UI
        const int64_t lockStartUs = esp_timer_get_time();
#endif
        const BaseType_t mutexTaken = xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(10));
#if APP_PROFILE_UI
        ::PerfCounters::recordSample(::PerfCounters::MUTEX_WAIT,
                                     static_cast<uint32_t>(esp_timer_get_time() - lockStartUs));
#endif
        if (mutexTaken != pdTRUE) {
            LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        } else {
            // lv_tick_inc() is driven by the esp_timer set up in setup() —
            // keeping it out of this loop means animations stay wall-clock
            // accurate even when the UI task overruns.
            TouchDriver::poll();

            // Explicit set wins over toggle when both are pending in the same
            // tick — the explicit command carries the user's literal intent.
            const bool prevIsDay = ThemeManager::isDayMode();

#if APP_BLE_ENABLED
            const int8_t bleSet = BleServer::takePendingDayNightSet();
            if (bleSet >= 0) {
                ThemeManager::setDayMode(bleSet == 1);
                // Drop any stale toggle from the same client to avoid undoing
                // the explicit set immediately after.
                (void)BleServer::takePendingDayNightToggle();
            } else if (BleServer::takePendingDayNightToggle()) {
                ThemeManager::toggleDayMode();
            }
#endif
            const int8_t usbSet = UsbComm::takePendingDayNightSet();
            if (usbSet >= 0) {
                ThemeManager::setDayMode(usbSet == 1);
                (void)UsbComm::takePendingDayNightToggle();
            } else if (UsbComm::takePendingDayNightToggle()) {
                ThemeManager::toggleDayMode();
            }

            didDayNightChange = (ThemeManager::isDayMode() != prevIsDay);

            PageManager::updateWidgets();
            {
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

            xSemaphoreGive(g_lvglMutex);
        }

#if !APP_SIMULATION_MODE
        // Issue #666 — feed the Task WDT once per UI tick. Placed after
        // lv_task_handler() (the slowest leg of the loop) so genuine LVGL
        // deadlocks deeper in the iteration still trip the watchdog.
        esp_task_wdt_reset();
#endif

#if APP_BLE_ENABLED
        // Notify STATUS after releasing LVGL mutex so ThemeManager::isDayMode() is stable
        if (didDayNightChange) {
            BleServer::pushStatusNotify();
        }
#else
        (void)didDayNightChange;
#endif

#if APP_PROFILE_UI
        // Frame-total wall time, captured before the delay so the metric
        // measures useful work — not the deliberate sleep.
        const int64_t frameEndUs = esp_timer_get_time();
        ::PerfCounters::recordSample(::PerfCounters::FRAME_TOTAL,
                                     static_cast<uint32_t>(frameEndUs - frameStartUs));
        // Frame-miss heuristic: if the delta between consecutive `lastWake`
        // values exceeds the configured period by >2 ms, the previous frame
        // overran its deadline.
        const TickType_t prevWake = lastWake;
        const TickType_t nowTicks = xTaskGetTickCount();
        if ((nowTicks - prevWake) > pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS + 2)) {
            ::PerfCounters::recordFrameMiss();
        }
        ::PerfCounters::tick();
#endif

#if APP_LV_TASK_LOG
        {
            const int64_t _now = esp_timer_get_time();
            if (s_lvLastFlushUs == 0)
                s_lvLastFlushUs = _now;
            if (_now - s_lvLastFlushUs >= 1000000) {
                const uint32_t avg = s_lvCount ? (s_lvSumUs / s_lvCount) : 0;
                LOG_INFO("PERF", "lv_task: avg=%uus max=%uus n=%u", avg,
                         static_cast<unsigned>(s_lvMaxUs), static_cast<unsigned>(s_lvCount));
                s_lvSumUs = 0;
                s_lvMaxUs = 0;
                s_lvCount = 0;
                s_lvLastFlushUs = _now;
            }
        }
#endif

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS));
    }
}

// ---------------------------------------------------------------------------
// CAN task
// ---------------------------------------------------------------------------

void taskCAN(void *pvParameters) {
    // CanManager::initHardware() is called from BootSequence::run() before tasks start.
    // This task only runs the receive/dispatch loop.
    // vTaskDelay(CAN_TASK_YIELD_TICKS) keeps IDLE0 alive on a busy bus where
    // twai_receive returns immediately every iteration (issue #200).
    while (true) {
        CanManager::tick();
#if !APP_SIMULATION_MODE
        // Issue #666 — CAN task WDT feed. CanManager::tick() blocks up to
        // 10 ms in twai_receive and may sleep 100 ms while retrying install;
        // both are well within TASK_WDT_TIMEOUT_MS.
        esp_task_wdt_reset();
#endif
        vTaskDelay(CAN_TASK_YIELD_TICKS);
    }
}

// ---------------------------------------------------------------------------
// USB communication task
// ---------------------------------------------------------------------------

void taskUSBComm(void *pvParameters) {
    UsbComm::init();

    while (true) {
        UsbComm::tick();
#if !APP_SIMULATION_MODE
        // Issue #666 — USB task WDT feed. Default tick cadence is 20 ms,
        // worst case (CMD_PUT_CONFIG burn under LVGL mutex) ~200 ms, both
        // far below TASK_WDT_TIMEOUT_MS.
        esp_task_wdt_reset();
#endif
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---------------------------------------------------------------------------
// BLE task — advertising + telemetry notifications
// ---------------------------------------------------------------------------
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
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BLE_TELE_INTERVAL_MS));
    }
}

#endif

// ---------------------------------------------------------------------------
// Simulation task
// ---------------------------------------------------------------------------
#if APP_SIMULATION_MODE

void taskSim(void *pvParameters) {
    SimEngine::init();

    while (true) {
        SimEngine::tick();
        vTaskDelay(pdMS_TO_TICKS(SIM_UPDATE_MS));
    }
}

#endif
