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
// canshift-firmware/package.json — single source of truth across the release.
// The fallback below only triggers if the script fails (and prints a warning).
#ifndef APP_VERSION_STR
    #define APP_VERSION_STR "0.0.0-unset"
#endif

// ---------------------------------------------------------------------------
// Build mode flags
// These are set either here or via platformio.ini build_flags.
// platformio.ini overrides take precedence (defined before this header).
// ---------------------------------------------------------------------------

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
// Task priorities (0=lowest, configMAX_PRIORITIES-1=highest)
// ESP32 Arduino framework default configMAX_PRIORITIES is 25.
#define TASK_PRIO_UI 10   // UI rendering — moderate priority
#define TASK_PRIO_CAN 15  // CAN parsing — higher, time-sensitive
#define TASK_PRIO_USB 8   // USB sync — lower, not time-critical
#define TASK_PRIO_INPUT 7 // GPIO button polling — below UI (#833)

// Task core pinning — ESP32 has 2 cores (0 and 1)
// Core 1 (APP_CPU) is typically used for Arduino loop/app code
// Core 0 (PRO_CPU) runs the WiFi/BT stack; avoid if using wireless
#define TASK_CORE_UI 1
#define TASK_CORE_CAN 0
#define TASK_CORE_USB 1
#define TASK_CORE_INPUT 0 // Pinned to core 0 to keep UI core jitter-free (#833)

#ifndef TASK_STACK_INPUT
    #define TASK_STACK_INPUT 2048 // GPIO polling + dispatch — tiny stack
#endif

// Watchdog timeout for UI/CAN/USB tasks. Long enough to survive page
// rebuild (#717 instrumentation showed up to ~1.5s on cold cache) and
// SPIFFS-backed font load (~600ms each, 6 fonts).
#define TASK_WDT_TIMEOUT_MS 8000U

// Pre-`esp_restart()` flush delay. Gives the UART TX FIFO / NimBLE notify
// queue / WiFi OTA response a deterministic window to drain before the
// MCU resets. 200 ms is the largest historical value across the three
// reboot sites (rotation save, BLE reboot cmd, OTA finalize) and is now
// the single source of truth (F-ME-10).
#define PRE_RESTART_FLUSH_DELAY_MS 200U

// ---------------------------------------------------------------------------
// USB task tracing (issue #976)
// ---------------------------------------------------------------------------
//
// Lightweight instrumentation around the USB task tick to localise the WDT
// timeout reported in #976. Off by default — flip via `-D APP_USB_TICK_TRACE=1`
// in platformio.ini (or `build_flags` on a custom env) when reproducing.
//
// When enabled, the task loop logs:
//   - the interval between two consecutive `UsbComm::tick()` calls (gap),
//   - the body duration of each tick,
// each gated by a threshold so the log surface stays quiet under steady state.

#ifndef APP_USB_TICK_TRACE
    #define APP_USB_TICK_TRACE 0
#endif

// Warn when `UsbComm::tick()` body takes longer than this (µs). 1 s leaves
// 7 s of WDT headroom — plenty of margin to surface a hang before the
// watchdog actually fires.
#define USB_TICK_DURATION_WARN_US 1000000UL

// Warn when the gap between two USB-tick entries exceeds this (µs). The
// nominal cadence is 20 ms (`vTaskDelay(pdMS_TO_TICKS(20))`); 200 ms = 10×
// nominal, which is well below the watchdog threshold but high enough to
// flag genuine starvation rather than scheduler jitter.
#define USB_TICK_INTERVAL_WARN_US 200000UL

// Abort the firmware when CAN-scan queue allocation fails so the failure
// surfaces as an explicit panic instead of a silent degraded state where
// `s_canScanQueue` stays nullptr (issue #976 smoking gun #1). Off by default
// — only enable during repro runs because every queue alloc failure now
// crashes the device.
#ifndef APP_USB_CAN_SCAN_FAIL_LOUD
    #define APP_USB_CAN_SCAN_FAIL_LOUD 0
#endif

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
// any pending button click underneath the finger (issue #640). Tuned just
// above the expected jitter of a stationary tap on the resistive XPT2046
// (~4 px) so even a short flick of the finger across a button-filled page
// commits to a swipe (issue #1262). The gesture is also fired directly from
// the cancel path so a tap that crosses this threshold navigates without
// waiting for LVGL's slower cumulative gesture_limit.
#define SWIPE_CANCEL_THRESHOLD_PX 8

// Grace window (ms) after a toggle button click during which update() must
// NOT overwrite the local latch from the bound signal. Lets the ECU echo
// arrive without flickering or re-arming on re-tap. See issue #658.
#define BUTTON_SIGNAL_SYNC_GRACE_MS 500

// Touch-press → click latency warn threshold (µs). Crossing this fires a
// single `LOG_WARN("TOUCH", "press→click slow: …")` for the offending click.
// Always compiled (issue #1256) — the full APP_PROFILE_UI histogram still
// only runs in dev builds. 80 ms covers two LVGL handler periods plus
// generous slack for a normal mutex-take + draw cycle, so a healthy
// dashboard never trips. The destructive theme-toggle rebuild used to push
// press→click past 200 ms; #1257's in-place reskin brings it to sub-ms,
// leaving the budget for genuine regressions.
#ifndef APP_TOUCH_LATENCY_WARN_US
    #define APP_TOUCH_LATENCY_WARN_US 80000U
#endif

// Suppression window (ms) after Settings opens during which a click on the
// top bar must NOT close it. A swipe-down that opens Settings is followed by
// LVGL's click event for the same touch — without this guard the panel
// opens and immediately closes again (issue #909).
#define SETTINGS_OPEN_TAP_GUARD_MS 300

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
// + recursive mutex). 512 sits at the upper bound of the abort range with
// no extra safety margin — bumped down from 768 (#1242) because the prior
// margin was conservative enough to refuse legitimate icon loads whenever
// `largest_free` dipped just under 768 B mid-buildPage, leaving button
// icons + the day/night toggle blank on the user-visible controls page.
// `icon_assets.cpp::preloadDashboardAssets()` uses the same constant so
// preload and on-demand loads agree on the gate threshold; previously the
// two paths disagreed (preload: 512, widget: 768) which caused an icon to
// be preloaded into the cache only to have the widget refuse to render it
// against the same heap a few ms later. If the abort threshold turns out
// to be higher on a future newlib bump, raise this back. The threshold is
// only relevant on no-PSRAM ESP32 — boards with PSRAM have `largest_free`
// in the hundreds of KB throughout.
// Lowered 512→256 after observing CAN_NO_MEM at boot starves heap and the
// 512 gate refused every icon preload (2026-06-02). The newlib __sfp abort
// risk is real but on this device the actual abort threshold is around 200 B
// internal-cap free, so 256 keeps a margin while letting icons load.
#ifdef __cplusplus
static constexpr size_t LVGL_FS_MIN_HEAP_BYTES = 256;
#else
    #define LVGL_FS_MIN_HEAP_BYTES 256
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

// CAN classic frame payload cap (bytes). Used by the parser, manager TX
// path, and signals.json validation to clamp/reject out-of-range byte
// offsets. CAN FD would lift this to 64 — also fits in uint8_t.
// Promoted from three local copies (one of which was uint16_t) so every
// site shares a single source of truth (F-LO-3).
#ifdef __cplusplus
static constexpr uint8_t kCanFrameMaxBytes = 8;
#else
    #define CAN_FRAME_MAX_BYTES 8U
#endif

// CAN receive queue depth (frames)
#define CAN_RX_QUEUE_DEPTH 32

// Yield delay applied at the end of each taskCAN iteration (FreeRTOS ticks).
// Required because twai_receive returns immediately on a busy bus, which
// otherwise starves IDLE0 at TASK_PRIO_CAN=15 and trips the Task Watchdog
// Timer. 1 tick (~1 ms at configTICK_RATE_HZ=1000) is enough for IDLE0
// to run while staying well below typical ECU group cadence (issue #200 —
// originally tuned against MaxxECU; applies equally to other CAN-bus ECUs).
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
// OBD-II polling (issue #841 — phase 3 of #556)
// ---------------------------------------------------------------------------

// Standard OBD-II frame IDs for the v1 single-ECU path. The dash broadcasts
// requests on the functional ID and decodes responses from the ECM at 0x7E8.
// Multi-ECU (0x7E9..0x7EF) + ISO-TP multi-frame are deferred.
#define OBD2_REQUEST_FRAME_ID 0x7DFU
#define OBD2_RESPONSE_FRAME_ID 0x7E8U

// Mirror of canshift-core OBD2_{MIN,MAX}_INTERVAL_MS. Kept in lockstep so
// the config_loader can reject out-of-range JSON locally rather than waiting
// for the Studio validator. _FW suffix avoids colliding with a future
// auto-generated header that might define the same name.
#define OBD2_MIN_INTERVAL_MS_FW 100U
#define OBD2_MAX_INTERVAL_MS_FW 60000U

// Maximum number of distinct polling slots the firmware tracks. The poller
// uses one slot per signal that carries a `polling` block — caps at
// `CONFIG_MAX_SIGNALS` since a single signal cannot have more than one
// polling configuration. Promoted to a named constant so the static storage
// in obd2_poller.cpp stays in sync with the upstream signal cap.
#define OBD2_MAX_POLL_SLOTS CONFIG_MAX_SIGNALS

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
// Each CfgWidget is ~264 bytes; each page reserves CONFIG_MAX_WIDGETS_PER_PAGE
// of them → ~6.2 KB BSS per page. #1357 tried 4→8 to fit cruise_control, but
// the +25 KB hit fragmented the runtime heap enough that CAN's twai_init
// task and the USB rxBuf alloc both started failing with ESP_ERR_NO_MEM at
// boot (reverted in #1358). The proper fix is heap-allocating the page array
// at load time so cap can grow without BSS pressure — tracked separately.
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
// Error store
// ---------------------------------------------------------------------------

// Depth of the firmware-side error ring buffer (diag/error_store.cpp).
// Promoted from a local constexpr so the Studio Error Bar UI and any other
// cross-package consumer can stay in sync with a single source of truth
// (F-ME-12). Raising this also widens FwError[] copies inside critical
// sections — keep small (≤16).
#define ERROR_STORE_RING_SIZE 6U

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
#ifndef APP_BLE_ENABLED
    #define APP_BLE_ENABLED 1
#endif

// Runtime default for the BLE-enabled NVS preference (`screen_cfg/ble_en`).
// ON by default — BLE is the primary mobile-app pairing path, the dash
// advertises out of the box. Mutually exclusive with the WiFi AP at boot:
// `BleServer::earlyInit()` checks `WifiAp::isAutoStartEnabled()` and skips
// BLE when WiFi is opted in, so the two never compete for ESP32 DRAM / radio.
// (Security note from #878 — the GATT surface is still unauthenticated until
// #873 lands; pairing is intentionally short-window via the MOBILE PAIRING
// toggle to limit exposure.)
#ifndef BLE_DEFAULT_ENABLED
    #define BLE_DEFAULT_ENABLED 1
#endif

// WiFi-AP-based OTA flow (started on demand from BLE). Phase 1 ships firmware
// over USB (esptool) so we don't need the AP / HTTP / Update.h infrastructure.
// When 0, wifi_ap.cpp ships only stubs and the WiFi / WebServer / Update Arduino
// libs are not linked — saves ~80 KB flash. Re-enable when phase 3 (BLE-driven
// OTA) needs it (issue #48).
#ifndef APP_WIFI_OTA_ENABLED
    #define APP_WIFI_OTA_ENABLED 0
#endif

// Dash-hosted Studio SPA — when 1, wifi_ap.cpp registers a static-file
// route table that serves the gzipped browser bundle embedded via
// board_build.embed_files. When 0, the SPA routes drop out (no embed
// symbols pulled in, no flash cost). Requires APP_WIFI_OTA_ENABLED=1 to
// have a WebServer to attach to — gated at compile time in wifi_ap.cpp.
// Issue #1077 phase 4.
#ifndef APP_SPA_SERVE
    #define APP_SPA_SERVE 0
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

// WiFi TCP server task (started/stopped alongside the AP via WifiAp::start/stop).
// Lives on core 1 alongside the AP HTTP server task; both share the same
// Arduino-WiFi stack. 4 KB stack matches the AP task — single-client polling
// loop with a USB_RX_BUF_SIZE line buffer in BSS, so the stack only carries
// FreeRTOS overhead + a handful of locals. Issue #1071.
#ifndef TASK_STACK_WIFI_TCP
    #define TASK_STACK_WIFI_TCP 4096
#endif
#define TASK_PRIO_WIFI_TCP 5
#define TASK_CORE_WIFI_TCP 1

// WiFi WebSocket server task (started/stopped alongside the AP via
// WifiAp::start/stop). Mirrors the TCP server's footprint — single-client
// polling loop driven by WebSocketsServer::loop(); the library carries its
// own per-client buffers in BSS, so the FreeRTOS stack only needs room for
// the event dispatcher + Arduino String header parsing. Issue #1105.
#ifndef TASK_STACK_WIFI_WS
    #define TASK_STACK_WIFI_WS 4096
#endif
#define TASK_PRIO_WIFI_WS 5
#define TASK_CORE_WIFI_WS 1

// BLE telemetry notify interval
#define BLE_TELE_INTERVAL_MS 100 // 10Hz

// BLE heap thresholds — empirically tuned for the CrowPanel 2.8" reference
// board. Pre-init guard ensures NimBLE has room for the stack + advertising
// state; GATT-setup guard covers createServer + 4 characteristics + start().
// Promoted from `ble_server.cpp` so a board profile with a different DRAM
// budget can override per-env via build_flags (issue #909).
#define BLE_MIN_HEAP_BYTES (50U * 1024U)
#define BLE_GATT_MIN_HEAP_BYTES (24U * 1024U)

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
// Diagnostics / logging
// ---------------------------------------------------------------------------

// Log level: 0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose
// Release default is 1 (error) — the firmware CLAUDE.md mandates zero
// serial output in release builds, and the previous default of 3 (info)
// streamed signal lifecycle / status lines over UART0 / USB-CDC that a
// race team would consider PII (rpm, throttle, gear, lambda, …). Bumping
// to error means the device only speaks when something actually broke.
// Issue #899. Debug builds keep `debug` so developer instrumentation
// stays visible without editing build flags.
#ifndef APP_LOG_LEVEL
    #if APP_DEBUG_BUILD
        #define APP_LOG_LEVEL 4
    #else
        #define APP_LOG_LEVEL 1
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
// per-signal-timeout chatter doesn't flood UART0. Enabled in [env:debug] so
// developers see the full stream while iterating. Independent of
// APP_LOG_LEVEL — when off, individual LOG_VDEBUG() call-sites collapse to
// no-ops at preprocess time even if APP_LOG_LEVEL >= 4.
#ifndef APP_VERBOSE_DEBUG_LOGS
    #if APP_DEBUG_BUILD
        #define APP_VERBOSE_DEBUG_LOGS 1
    #else
        #define APP_VERBOSE_DEBUG_LOGS 0
    #endif
#endif
