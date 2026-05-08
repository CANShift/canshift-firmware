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

#include "boot/boot_sequence.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"

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
// setup() — runs once on core 1 after reset
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(USB_SERIAL_BAUD);

    // Brief delay to let serial monitor connect
    delay(200);

    Logger::init();
    LOG_INFO("BOOT", "CANShift v" APP_VERSION_STR " starting");

#if APP_SIMULATION_MODE
    LOG_WARN("BOOT", "*** SIMULATION MODE ACTIVE — no CAN hardware ***");
#endif

    // Create the LVGL mutex before any LVGL calls
    g_lvglMutex = xSemaphoreCreateMutex();
    if (!g_lvglMutex) {
        LOG_ERROR("BOOT", "Failed to create LVGL mutex — halting");
        while (true) {
            delay(1000);
        }
    }

    // Run the synchronous boot sequence:
    //   1. Init HAL (display, touch, storage)
    //   2. Init LVGL
    //   3. Load config from filesystem
    //   4. Build the initial UI from config
    BootSequence::run();

    // Start the LVGL tick timer only after lv_init() has run inside BootSequence.
    startLvglTickTimer();

    // Initialize the perf-counter aggregator (no-op when APP_PROFILE_UI=0).
    PERF_INIT();

    LOG_INFO("BOOT", "Boot complete — starting tasks");

    // ---------------------------------------------------------------------------
    // UI task — runs LVGL tick and handler on core 1
    // All UI rendering and touch input processing happens here.
    // ---------------------------------------------------------------------------
    xTaskCreatePinnedToCore(taskUI, "ui", TASK_STACK_UI, nullptr, TASK_PRIO_UI, nullptr,
                            TASK_CORE_UI);

#if !APP_SIMULATION_MODE
    // ---------------------------------------------------------------------------
    // CAN task — reads TWAI frames, parses them, writes to SignalStore
    // Runs on core 0 to avoid contention with LVGL on core 1
    // ---------------------------------------------------------------------------
    xTaskCreatePinnedToCore(taskCAN, "can", TASK_STACK_CAN, nullptr, TASK_PRIO_CAN, nullptr,
                            TASK_CORE_CAN);
#else
    // ---------------------------------------------------------------------------
    // Simulation task — writes fake signal values to SignalStore
    // ---------------------------------------------------------------------------
    xTaskCreatePinnedToCore(taskSim, "sim", TASK_STACK_SIM, nullptr, TASK_PRIO_SIM, nullptr,
                            TASK_CORE_SIM);
#endif

    // ---------------------------------------------------------------------------
    // USB comm task — Phase 1 config sync from desktop app
    // ---------------------------------------------------------------------------
    xTaskCreatePinnedToCore(taskUSBComm, "usb", TASK_STACK_USB, nullptr, TASK_PRIO_USB, nullptr,
                            TASK_CORE_USB);

#if APP_BLE_ENABLED
    // ---------------------------------------------------------------------------
    // BLE task — Phase 3 mobile app (telemetry + settings + WiFi AP OTA trigger)
    // ---------------------------------------------------------------------------
    xTaskCreatePinnedToCore(taskBLE, "ble", TASK_STACK_BLE, nullptr, TASK_PRIO_BLE, nullptr,
                            TASK_CORE_BLE);
#endif

    LOG_INFO("BOOT", "All tasks started");
}

// ---------------------------------------------------------------------------
// loop() — Arduino main loop. We use FreeRTOS tasks instead.
// This runs on the main task with lowest priority after task creation.
// ---------------------------------------------------------------------------
void loop() {
    // Nothing to do here — all work is in FreeRTOS tasks.
    // Yield to avoid starving lower-priority tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ---------------------------------------------------------------------------
// UI task
// ---------------------------------------------------------------------------
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "ui/page_manager.h"
#include "ui/theme_manager.h"
#include "runtime/alert_engine.h"
#include <lvgl.h>
#include "hal/usb/usb_comm.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
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
#endif
        if (UsbComm::takePendingCalibration()) {
            TouchDriver::calibrate();
            calibratedThisTick = true;
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
                lv_task_handler();
            }

            // SD hot-plug recovery (issue #251) and eject detection
            // (issue #315): two complementary slow polls that share the
            // LVGL mutex window because both touch widgets (SD badge) and
            // the SPI bus shared with the TFT. Mutually exclusive — only
            // one branch can fire per tick because the SD is either
            // mounted or not.
            const uint32_t nowMs = millis();
            if (BootSequence::isDegradedNoSd()) {
                static uint32_t lastSdProbeMs = 0;
                if (nowMs - lastSdProbeMs >= SD_HOTPLUG_POLL_INTERVAL_MS) {
                    lastSdProbeMs = nowMs;
                    BootSequence::tryRecoverSd();
                }
            } else {
                static uint32_t lastSdEjectMs = 0;
                if (nowMs - lastSdEjectMs >= SD_EJECT_POLL_INTERVAL_MS) {
                    lastSdEjectMs = nowMs;
                    BootSequence::detectSdEject();
                }
            }

            xSemaphoreGive(g_lvglMutex);
        }

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

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS));
    }
}

// ---------------------------------------------------------------------------
// CAN task
// ---------------------------------------------------------------------------
#include "can/can_manager.h"

void taskCAN(void *pvParameters) {
    // CanManager::initHardware() is called from BootSequence::run() before tasks start.
    // This task only runs the receive/dispatch loop.
    // vTaskDelay(CAN_TASK_YIELD_TICKS) keeps IDLE0 alive on a busy bus where
    // twai_receive returns immediately every iteration (issue #200).
    while (true) {
        CanManager::tick();
        vTaskDelay(CAN_TASK_YIELD_TICKS);
    }
}

// ---------------------------------------------------------------------------
// USB communication task (Phase 1)
// ---------------------------------------------------------------------------
#include "hal/usb/usb_comm.h"

void taskUSBComm(void *pvParameters) {
    UsbComm::init();

    while (true) {
        UsbComm::tick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---------------------------------------------------------------------------
// BLE task — advertising + telemetry notifications
// ---------------------------------------------------------------------------
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"

void taskBLE(void *pvParameters) {
    BleServer::init();

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        BleServer::tick();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BLE_TELE_INTERVAL_MS));
    }
}
#endif

// ---------------------------------------------------------------------------
// Simulation task
// ---------------------------------------------------------------------------
#if APP_SIMULATION_MODE
    #include "sim/sim_engine.h"

void taskSim(void *pvParameters) {
    SimEngine::init();

    while (true) {
        SimEngine::tick();
        vTaskDelay(pdMS_TO_TICKS(SIM_UPDATE_MS));
    }
}
#endif
