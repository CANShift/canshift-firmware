#pragma once
// app_config.h — Application-level configuration flags and constants
//
// Hardware pin assignments → board_config.h
// LVGL settings          → lv_conf.h
// Board capabilities     → hardware_profile.h

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
// APP_VERSION_STR is injected at build time by scripts/extra_targets.py from
// canshift-studio/package.json — single source of truth across the release.
// The fallback below only triggers if the script fails (and prints a warning).
#ifndef APP_VERSION_STR
    #define APP_VERSION_STR "0.0.0-unset"
#endif

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

// Secure-boot v2 + flash-encryption build — set by [env:secure] in
// platformio.ini. Off in every other env. The macro is exposed for
// downstream gating (e.g., a future boot-log line confirming the running
// image is the signed/encrypted variant). No runtime behaviour change yet —
// the actual fuse-burn happens host-side via scripts/secure_boot_first_flash.sh.
// See docs/secure-boot-setup.md.
#ifndef APP_SECURE_BOOT_BUILD
    #define APP_SECURE_BOOT_BUILD 0
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

// Watchdog timeout for UI/CAN/USB tasks. Long enough to survive page
// rebuild (#717 instrumentation showed up to ~1.5s on cold cache) and
// SPIFFS-backed font load (~600ms each, 6 fonts).
#define TASK_WDT_TIMEOUT_MS 8000U

// ---------------------------------------------------------------------------
// LVGL timing
// ---------------------------------------------------------------------------

// LVGL tick rate in milliseconds — drives lv_tick_inc()
// Higher = smoother animations but more CPU load
// Used by the esp_timer periodic callback installed in setup() (main.cpp).
#define LVGL_TICK_MS 5 // 5ms = 200 Hz tick

// LVGL task period — how often lv_task_handler() is called.
// Keep this ≥ LVGL_TICK_MS. 10 ms = ~100 Hz UI loop, which gives the touch
// layer twice as many opportunities to dispatch click events per second
// (issue #95, fix F2). With the F1 bulk SignalStore snapshot in place the
// per-iteration cost is well below 10 ms in steady state.
#define LVGL_HANDLER_PERIOD_MS 10

// Horizontal travel (in px) that reclassifies a press as a swipe and cancels
// any pending button click underneath the finger (issue #640). Tuned above the
// expected jitter of a stationary tap on the resistive XPT2046 (~4 px) yet
// well below a deliberate page swipe so navigation still feels responsive.
#define SWIPE_CANCEL_THRESHOLD_PX 12

// Grace window (ms) after a toggle button click during which update() must
// NOT overwrite the local latch from the bound signal. Lets the ECU echo
// arrive without flickering or re-arming on re-tap. See issue #658.
#define BUTTON_SIGNAL_SYNC_GRACE_MS 500

// ---------------------------------------------------------------------------
// LVGL display buffers
// Two draw buffers allow double-buffered rendering.
// Each buffer is (width * height * bytes_per_pixel) for full-screen.
// For 320x240x2 bytes = 153,600 bytes per full buffer — too large for IRAM.
// Use partial buffers: N lines × width × 2 bytes.
// ---------------------------------------------------------------------------
// LVGL draw-buffer line count is derived from HW_LVGL_DRAW_BUDGET_BYTES +
// HW_DISPLAY_WIDTH in display_driver.cpp::computeLvglBufLines

// Minimum largest-free-block to allow an LVGL FS open. Below this, return
// nullptr to keep newlib __sfp out of abort() territory. See issue #651.
// Empirically, newlib's real abort threshold is ~256-512 bytes (FILE struct
// + recursive mutex). 1024 leaves headroom for transient fragmentation
// without prematurely refusing legitimate font/icon loads — the original
// boot trace from #651 shows fonts opening with largest_free=1620, which a
// 4096 guard would have rejected. See #660.
#ifdef __cplusplus
static constexpr size_t LVGL_FS_MIN_HEAP_BYTES = 1024;
#else
    #define LVGL_FS_MIN_HEAP_BYTES 1024
#endif

// ---------------------------------------------------------------------------
// Signal store
// ---------------------------------------------------------------------------

// Maximum number of signals the runtime store can hold
#define SIGNAL_STORE_MAX_SIGNALS 32

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

// Yield delay applied at the end of each taskCAN iteration (FreeRTOS ticks).
// Required because twai_receive returns immediately on a busy bus, which
// otherwise starves IDLE0 at TASK_PRIO_CAN=15 and trips the Task Watchdog
// Timer. 1 tick (~1 ms at configTICK_RATE_HZ=1000) is enough for IDLE0
// to run while staying well below the MaxxECU group cadence (issue #200).
#define CAN_TASK_YIELD_TICKS 1

// TWAI init retry policy (issue #652). When initHardware() fails at boot
// (typically ESP_ERR_NO_MEM from a tight heap), the CAN manager retries on a
// timer instead of leaving the driver uninstalled and spamming
// ESP_ERR_INVALID_STATE from twai_receive() at tick rate. Defined as macros so
// app_config.h stays C-compatible (it is included from a few C translation
// units alongside C++).
#define TWAI_INIT_RETRY_MS 5000U
#define TWAI_INIT_MAX_RETRIES 6U

// ---------------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------------

// Maximum JSON document size for each config file (bytes)
// Sized for realistic configs (4 pages × 12 widgets, schema v1.11+) plus
// growth headroom. The dashboard buffer also doubles as the USB receive
// buffer for CMD_PUT_CONFIG (single-line burn from Studio); a real-world
// burn at schema v1.11 is ~13 KB, so the previous 6 KB cap caused buffer
// overflow → parse_error. Bumped to 16 KB.
#define CONFIG_JSON_DOC_DASHBOARD 16384
#define CONFIG_JSON_DOC_SIGNALS 4096

// Maximum number of pages, widgets, and signals
// NOTE: CfgDashboard and CfgPage are statically sized arrays in BSS.
// Each CfgWidget is ~264 bytes. Reducing these frees ~26 KB of DRAM to fit
// NimBLE + WiFi alongside the application on the ESP32's 320 KB DRAM.
// A 320×240 display is not well-served by more than 4 pages or 12 widgets anyway.
#define CONFIG_MAX_PAGES 4
#define CONFIG_MAX_WIDGETS_PER_PAGE 12
#define CONFIG_MAX_SIGNALS 32

// Maximum stops a per-signal `colorRamp` may carry. Mirrored from
// canshift-core MAX_RAMP_STOPS (issue #430). 8 stops easily covers
// blue→green→amber→red gradients with extra hues while keeping the per-signal
// CfgColorRamp fixed-size and value-copyable into widget tags.
#define CFG_MAX_RAMP_STOPS 8

// First-boot provisioning of the embedded default configs to SPIFFS (issue
// #173). When 1, BootSequence checks for missing canonical config files
// after storage mounts and writes the firmware-baked defaults. Existing user
// data is never overwritten. The defaults are linked in via
// `board_build.embed_files`.
#ifndef DEFAULT_CONFIG_PROVISION_ENABLED
    #define DEFAULT_CONFIG_PROVISION_ENABLED 1
#endif

// ---------------------------------------------------------------------------
// Alert engine
// ---------------------------------------------------------------------------

// Rev limiter flash effect
// Flash the screen red when RPM exceeds this percentage of the configured limit
#define ALERT_REVLIMIT_WARN_PCT 95   // Warning: 95% of rev limit
#define ALERT_REVLIMIT_FLASH_PCT 100 // Full flash: at/over rev limit
#define ALERT_REVLIMIT_FLASH_HZ 8    // Flash frequency

// Battery voltage alert fallbacks — used only when signals.json does not
// configure per-signal thresholds for `battery_volts`. Defaults match the
// previously hardcoded magic numbers (lead-acid 12 V system, ~14 V float).
// Override per engine by setting warningLevel/dangerLevel/highWarningLevel
// on the battery_volts signal definition in signals.json.
#define BATTERY_DEFAULT_LOW_WARN_V 12.0f  // Below this = WARNING (battery weak)
#define BATTERY_DEFAULT_LOW_CRIT_V 11.5f  // Below this = CRITICAL (will not crank)
#define BATTERY_DEFAULT_HIGH_WARN_V 15.0f // Above this = WARNING (charging fault)
#define BATTERY_DEFAULT_HIGH_CRIT_V 16.0f // Above this = CRITICAL (regulator failure / overvoltage)

// ---------------------------------------------------------------------------
// BLE (Phase 3 — mobile app)
// ---------------------------------------------------------------------------

// Set to 0 to exclude NimBLE and WiFi from the build (saves ~30 KB DRAM).
// Disabled in [env:sim] because the sim environment doesn't need wireless.
#ifndef APP_BLE_ENABLED
    #define APP_BLE_ENABLED 1
#endif

// WiFi-AP-based OTA flow (started on demand from BLE). Phase 1 ships firmware
// over USB (esptool) so we don't need the AP / HTTP / Update.h infrastructure.
// When 0, wifi_ap.cpp ships only stubs and the WiFi / WebServer / Update Arduino
// libs are not linked — saves ~80 KB flash. Re-enable when phase 3 (BLE-driven
// OTA) needs it (issue #48).
#ifndef APP_WIFI_OTA_ENABLED
    #define APP_WIFI_OTA_ENABLED 0
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
// Password is per-device, generated on first boot and persisted in NVS
// (namespace "wifi_ap", key "pwd"). Surfaced via BLE STATUS field "ap_password".
#define BLE_WIFI_AP_TIMEOUT_MS (5UL * 60UL * 1000UL) // 5 minutes

// ---------------------------------------------------------------------------
// OTA upload integrity (issue #205 part 2)
// ---------------------------------------------------------------------------

// Require an HMAC-SHA256 trailer on every OTA upload. The trailer is the
// last 32 bytes of the upload and must equal HMAC_SHA256(firmware, secret).
// Production builds ALWAYS require the trailer: the firmware will reject any
// OTA payload that is either missing the trailer or that carries a bad HMAC,
// returning HTTP 500 with reason="hmac" and aborting Update.write. Combined
// with the per-request bearer token derived from the AP password in
// wifi_ap.cpp, this closes issue #667 (unauthenticated firmware writes on
// the device's softAP). Overriding to 0 is intentionally not supported via
// the public build envs — secrets.ini hard-fails the build if the OTA secret
// is the dev placeholder for prod flavours (scripts/extra_targets.py).
#ifndef APP_OTA_REQUIRE_HMAC
    #define APP_OTA_REQUIRE_HMAC 1
#endif

// Build-time shared secret used to verify OTA HMAC trailers. The real value
// is injected via secrets.ini (gitignored) by scripts/extra_targets.py; the
// fallback below is the dev placeholder and MUST NOT be used in production
// builds — replace it before any release that flips APP_OTA_REQUIRE_HMAC=1.
// extra_targets.py warns loudly when the fallback is hit.
#ifndef OTA_HMAC_SECRET
    #define OTA_HMAC_SECRET "DEV_INSECURE_REPLACE_BEFORE_PROD"
#endif

// ---------------------------------------------------------------------------
// USB config sync (Phase 1)
// ---------------------------------------------------------------------------

// USB RX buffer — must fit the largest inbound command.
// PUT_CONFIG wraps dashboard JSON plus {"cmd":2,"payload":} framing (~256 B overhead).
// Reusing this buffer for TX serialization in handlePutConfig avoids a second large static.
#define USB_RX_BUF_SIZE (CONFIG_JSON_DOC_DASHBOARD + 256)

// Protocol version — increment when USB wire protocol changes
// v2: LOG_* macros now emit `{"log":1,...}` envelopes instead of `[I][TAG]`
//     plain text; UART0 writes from logger and wire protocol are serialized
//     under a shared mutex (issue #199).
#define USB_PROTOCOL_VERSION 2

// How long the burn-overlay error state stays visible before it tears
// itself down and uncovers the dashboard underneath. Long enough for the
// driver to read the message at a glance, short enough that nothing
// important is hidden if they look away (issue #189).
#define BURN_OVERLAY_ERROR_HOLD_MS 3000

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
#endif // APP_SIMULATION_MODE

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

// APP_PROFILE_UI — enables per-frame UI instrumentation: mutex wait, widget
// updates, LVGL handler dt, frame-total, FPS, frame-misses. Aggregated and
// emitted as a single 1 Hz LOG_INFO("PERF", …) line by PerfCounters::tick().
// Set via [env:debug-perf] in platformio.ini. See also APP_LV_TASK_LOG below
// for a lighter-weight subset.

// Optional lv_task_handler() duration logging — emits a 1 Hz "lv_task: avg/max/n"
// line from the UI task. Orthogonal to APP_PROFILE_UI; both can be enabled
// simultaneously. Off by default — zero overhead when disabled.
#ifndef APP_LV_TASK_LOG
    #define APP_LV_TASK_LOG 0
#endif

// Verbose per-event debug logs. Kept off in release so per-touch / per-gesture /
// per-signal-timeout chatter doesn't flood UART0. Enabled in [env:debug] and
// [env:sim] so developers see the full stream while iterating. Independent of
// APP_LOG_LEVEL — when off, individual LOG_VDEBUG() call-sites collapse to
// no-ops at preprocess time even if APP_LOG_LEVEL >= 4.
#ifndef APP_VERBOSE_DEBUG_LOGS
    #if APP_DEBUG_BUILD || APP_SIMULATION_MODE
        #define APP_VERBOSE_DEBUG_LOGS 1
    #else
        #define APP_VERBOSE_DEBUG_LOGS 0
    #endif
#endif
