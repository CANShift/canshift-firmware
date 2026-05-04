// main.cpp — Firmware entry point
// Boot sequence, FreeRTOS task creation, and main loop.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "app_config.h"
#include "board_config.h"
#include "hardware_profile.h"

#include "boot/boot_sequence.h"
#include "diag/logger.h"

// Task function forward declarations
void taskUI(void *pvParameters);
void taskCAN(void *pvParameters);
void taskUSBComm(void *pvParameters);

#if APP_SIMULATION_MODE
void taskSim(void *pvParameters);
#endif

// ---------------------------------------------------------------------------
// Global LVGL mutex
// LVGL is NOT thread-safe. All LVGL calls from any task must hold this mutex.
// ---------------------------------------------------------------------------
SemaphoreHandle_t g_lvglMutex = nullptr;

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
#include "runtime/alert_engine.h"
#include <lvgl.h>

void taskUI(void *pvParameters) {
    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        // Acquire LVGL mutex before any LVGL call
        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        } else {
            // Advance LVGL tick
            lv_tick_inc(LVGL_HANDLER_PERIOD_MS);

            // Process touch input
            TouchDriver::poll();

            // Pull latest signal values into all widgets.
            // Also checks signal timeouts and ticks the alert engine internally.
            PageManager::updateWidgets();

            // Run LVGL task handler (processes all pending events and redraws)
            lv_task_handler();

            xSemaphoreGive(g_lvglMutex);
        }

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
