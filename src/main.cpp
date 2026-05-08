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
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvglTickTimer,
                                             static_cast<uint64_t>(LVGL_TICK_MS) * 1000ULL));
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

        bool didDayNightToggle = false;

        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        } else {
            // lv_tick_inc() is driven by the esp_timer set up in setup() —
            // keeping it out of this loop means animations stay wall-clock
            // accurate even when the UI task overruns.
            TouchDriver::poll();

#if APP_BLE_ENABLED
            if (BleServer::takePendingDayNightToggle()) {
                ThemeManager::toggleDayMode();
                didDayNightToggle = true;
            }
#endif
            if (UsbComm::takePendingDayNightToggle()) {
                ThemeManager::toggleDayMode();
                didDayNightToggle = true;
            }

            PageManager::updateWidgets();
            lv_task_handler();
            xSemaphoreGive(g_lvglMutex);
        }

#if APP_BLE_ENABLED
        // Notify STATUS after releasing LVGL mutex so ThemeManager::isDayMode() is stable
        if (didDayNightToggle) {
            BleServer::pushStatusNotify();
        }
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
    while (true) {
        CanManager::tick();
        // tick() blocks on TWAI receive with a short timeout — no delay needed
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
