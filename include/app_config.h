#pragma once
// app_config.h — Application-level configuration flags and constants
//
// Hardware pin assignments → board_config.h
// LVGL settings          → lv_conf.h
// Board capabilities     → hardware_profile.h

#include <stdint.h>

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 1
#define APP_VERSION_PATCH 0
#define APP_VERSION_STR "0.2.1"

// ---------------------------------------------------------------------------
// Build mode flags
// These are set either here or via platformio.ini build_flags.
// platformio.ini overrides take precedence (defined before this header).
// ---------------------------------------------------------------------------

// Simulation mode — generate fake engine data, skip CAN hardware init
// Set to 1 for UI development without a connected ECU
#ifndef APP_SIMULATION_MODE
    #define APP_SIMULATION_MODE 0
#endif

// Debug build — enables extra logging and assertions
#ifndef APP_DEBUG_BUILD
    #define APP_DEBUG_BUILD 0
#endif

// ---------------------------------------------------------------------------
// FreeRTOS task configuration
// Adjust stack sizes if you see stack overflow panics (guru meditation error).
// ---------------------------------------------------------------------------

// Task stack sizes (bytes) — override via build_flags if needed
#ifndef TASK_STACK_UI
    #define TASK_STACK_UI 8192 // LVGL + UI render
#endif
#ifndef TASK_STACK_CAN
    #define TASK_STACK_CAN 4096 // CAN frame read + parse
#endif
#ifndef TASK_STACK_USB
    #define TASK_STACK_USB 4096 // USB serial config sync
#endif
#ifndef TASK_STACK_SIM
    #define TASK_STACK_SIM 2048 // Simulation signal generator
#endif

// Task priorities (0=lowest, configMAX_PRIORITIES-1=highest)
// ESP32 Arduino framework default configMAX_PRIORITIES is 25.
#define TASK_PRIO_UI 10  // UI rendering — moderate priority
#define TASK_PRIO_CAN 15 // CAN parsing — higher, time-sensitive
#define TASK_PRIO_USB 8  // USB sync — lower, not time-critical
#define TASK_PRIO_SIM 5  // Sim — lowest, best-effort

// Task core pinning — ESP32 has 2 cores (0 and 1)
// Core 1 (APP_CPU) is typically used for Arduino loop/app code
// Core 0 (PRO_CPU) runs the WiFi/BT stack; avoid if using wireless
#define TASK_CORE_UI 1
#define TASK_CORE_CAN 0
#define TASK_CORE_USB 1
#define TASK_CORE_SIM 1

// ---------------------------------------------------------------------------
// LVGL timing
// ---------------------------------------------------------------------------

// LVGL tick rate in milliseconds — drives lv_tick_inc()
// Higher = smoother animations but more CPU load
#define LVGL_TICK_MS 5 // 5ms = 200 Hz tick

// LVGL task period — how often lv_task_handler() is called
// Keep this ≥ LVGL_TICK_MS; 20ms = 50 FPS cap on UI refresh
#define LVGL_HANDLER_PERIOD_MS 20

// Display flush timeout watchdog (ms) — panic if flush takes longer
#define LVGL_FLUSH_TIMEOUT_MS 200

// ---------------------------------------------------------------------------
// LVGL display buffers
// Two draw buffers allow double-buffered rendering.
// Each buffer is (width * height * bytes_per_pixel) for full-screen.
// For 320x240x2 bytes = 153,600 bytes per full buffer — too large for IRAM.
// Use partial buffers: N lines × width × 2 bytes.
// ---------------------------------------------------------------------------
#define LVGL_BUF_LINE_COUNT 40 // Lines per draw buffer (40 lines = 25,600 bytes each)

// ---------------------------------------------------------------------------
// Signal store
// ---------------------------------------------------------------------------

// Maximum number of signals the runtime store can hold
#define SIGNAL_STORE_MAX_SIGNALS 64

// Default signal timeout — if no new CAN frame arrives within this period,
// mark the signal as invalid/stale
#define SIGNAL_DEFAULT_TIMEOUT_MS 1000

// Signal smoothing — simple exponential moving average weight (0.0-1.0)
// Higher = faster response, lower = smoother (more filtering)
// Applied to gauge-type widgets. Label and warning widgets use raw values.
#define SIGNAL_EMA_ALPHA 0.2f

// ---------------------------------------------------------------------------
// CAN / TWAI
// ---------------------------------------------------------------------------

// CAN receive queue depth (frames)
#define CAN_RX_QUEUE_DEPTH 32

// Maximum CAN frames parsed per task iteration
#define CAN_FRAMES_PER_TICK 16

// ---------------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------------

// Maximum JSON document size for each config file (bytes)
// Increase if configs grow larger
#define CONFIG_JSON_DOC_DASHBOARD 8192
#define CONFIG_JSON_DOC_SIGNALS 4096
#define CONFIG_JSON_DOC_THEME 2048

// Maximum number of pages, widgets, and signals
#define CONFIG_MAX_PAGES 8
#define CONFIG_MAX_WIDGETS_PER_PAGE 16
#define CONFIG_MAX_SIGNALS 64

// ---------------------------------------------------------------------------
// Alert engine
// ---------------------------------------------------------------------------

// Rev limiter flash effect
// Flash the screen red when RPM exceeds this percentage of the configured limit
#define ALERT_REVLIMIT_WARN_PCT 95   // Warning: 95% of rev limit
#define ALERT_REVLIMIT_FLASH_PCT 100 // Full flash: at/over rev limit
#define ALERT_REVLIMIT_FLASH_HZ 8    // Flash frequency

// Coolant temperature warning thresholds (°C)
// TODO: Tune for actual VR6 operating range
#define ALERT_COOLANT_WARN_C 100
#define ALERT_COOLANT_CRIT_C 110

// Oil temperature thresholds (°C)
#define ALERT_OIL_TEMP_WARN_C 120
#define ALERT_OIL_TEMP_CRIT_C 135

// Oil pressure thresholds (bar)
// TODO: Verify with VR6 oil pressure spec
#define ALERT_OIL_PRESS_WARN_BAR 1.5f
#define ALERT_OIL_PRESS_CRIT_BAR 1.0f

// ---------------------------------------------------------------------------
// BLE (Phase 3 — mobile app)
// ---------------------------------------------------------------------------

// Set to 0 to exclude NimBLE and WiFi from the build (saves ~30 KB DRAM).
// Disabled in [env:sim] because the sim environment doesn't need wireless.
#ifndef APP_BLE_ENABLED
    #define APP_BLE_ENABLED 1
#endif

#ifndef TASK_STACK_BLE
    #define TASK_STACK_BLE 5120 // NimBLE + ArduinoJson telemetry serialization
#endif
#define TASK_PRIO_BLE 6
#define TASK_CORE_BLE 1

// WiFi AP task (started on demand for OTA)
#ifndef TASK_STACK_WIFI
    #define TASK_STACK_WIFI 4096
#endif
#define TASK_PRIO_WIFI 5
#define TASK_CORE_WIFI 1

// BLE telemetry notify interval
#define BLE_TELE_INTERVAL_MS 100 // 10Hz

// WiFi AP configuration
#define BLE_WIFI_AP_PASSWORD "canshift"
#define BLE_WIFI_AP_TIMEOUT_MS (5UL * 60UL * 1000UL) // 5 minutes

// ---------------------------------------------------------------------------
// USB config sync (Phase 1)
// ---------------------------------------------------------------------------

// USB RX buffer — must fit the largest inbound command.
// PUT_CONFIG wraps dashboard JSON plus {"cmd":2,"payload":} framing (~256 B overhead).
// Reusing this buffer for TX serialization in handlePutConfig avoids a second large static.
#define USB_RX_BUF_SIZE (CONFIG_JSON_DOC_DASHBOARD + 256)

// Protocol version — increment when USB wire protocol changes
#define USB_PROTOCOL_VERSION 1

// ---------------------------------------------------------------------------
// Simulation mode config
// ---------------------------------------------------------------------------
#if APP_SIMULATION_MODE
    // Simulated RPM sweep parameters
    #define SIM_RPM_MIN 800
    #define SIM_RPM_MAX 7200
    #define SIM_RPM_STEP 20  // RPM change per update tick
    #define SIM_UPDATE_MS 50 // Simulation update interval

    // Simulated coolant temperature (°C, fixed for simplicity)
    #define SIM_COOLANT_C 90

    // Simulated oil temperature (°C)
    #define SIM_OIL_TEMP_C 95

    // Simulated oil pressure (bar)
    #define SIM_OIL_PRESS_BAR 3.5f

    // Simulated speed (km/h, derived from RPM in sim)
    #define SIM_GEAR_RATIO 3.2f // Approximate first gear equivalent
#endif                          // APP_SIMULATION_MODE

// ---------------------------------------------------------------------------
// Diagnostics / logging
// ---------------------------------------------------------------------------

// Log level: 0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose
#ifndef APP_LOG_LEVEL
    #if APP_DEBUG_BUILD
        #define APP_LOG_LEVEL 4
    #else
        #define APP_LOG_LEVEL 3
    #endif
#endif

// Log tag max length
#define LOG_TAG_MAX_LEN 16
