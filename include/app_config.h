#pragma once
// Companions: board_config.h (pins), lv_conf.h (LVGL), hardware_profile.h (caps).

#include <stddef.h>
#include <stdint.h>

// Injected at build time by scripts/extra_targets.py from package.json.
#ifndef APP_VERSION_STR
    #define APP_VERSION_STR "0.0.0-unset"
#endif

#ifndef APP_DEBUG_BUILD
    #define APP_DEBUG_BUILD 0
#endif

// Secure-boot v2 + flash-encryption build — fuse-burn is host-side via
// scripts/secure_boot_first_flash.sh.
#ifndef APP_SECURE_BOOT_BUILD
    #define APP_SECURE_BOOT_BUILD 0
#endif

#ifndef TASK_STACK_UI
    #define TASK_STACK_UI 8192
#endif
#ifndef TASK_STACK_CAN
    #define TASK_STACK_CAN 4096
#endif
#ifndef TASK_STACK_USB
    #define TASK_STACK_USB 4096
#endif
#define TASK_PRIO_UI 10
#define TASK_PRIO_CAN 15
#define TASK_PRIO_USB 8
#define TASK_PRIO_INPUT 7

#define TASK_CORE_UI 1
#define TASK_CORE_CAN 0
#define TASK_CORE_USB 1
// Pinned to core 0 to keep UI core jitter-free (#833).
#define TASK_CORE_INPUT 0

#ifndef TASK_STACK_INPUT
    #define TASK_STACK_INPUT 2048
#endif

// Long enough for page rebuild (~1.5s cold) + SPIFFS font loads (~600 ms × 6).
#define TASK_WDT_TIMEOUT_MS 8000U

// Drain window for UART/NimBLE/OTA before esp_restart() — largest historical
// value across the three reboot sites (F-ME-10).
#define PRE_RESTART_FLUSH_DELAY_MS 200U

// Instrumentation around USB tick to localise the WDT timeout (#976).
#ifndef APP_USB_TICK_TRACE
    #define APP_USB_TICK_TRACE 0
#endif

// 1 s leaves 7 s WDT headroom.
#define USB_TICK_DURATION_WARN_US 1000000UL

// 10× the 20 ms nominal cadence — flags starvation, not jitter.
#define USB_TICK_INTERVAL_WARN_US 200000UL

// Crashes loudly on CAN-scan queue alloc failure — repro only (#976).
#ifndef APP_USB_CAN_SCAN_FAIL_LOUD
    #define APP_USB_CAN_SCAN_FAIL_LOUD 0
#endif

#define LVGL_TICK_MS 5

// Keep ≥ LVGL_TICK_MS. 10 ms ≈ 100 Hz gives touch 2x dispatch budget (#95 F2).
#define LVGL_HANDLER_PERIOD_MS 10

// Just above XPT2046 stationary jitter (~4 px) so flicks commit to swipe (#640, #1262).
#define SWIPE_CANCEL_THRESHOLD_PX 8

// Lets ECU echo arrive without flickering the toggle (#658).
#define BUTTON_SIGNAL_SYNC_GRACE_MS 500

// Healthy dashboards stay sub-ms after #1257; 80 ms catches genuine regressions (#1256).
#ifndef APP_TOUCH_LATENCY_WARN_US
    #define APP_TOUCH_LATENCY_WARN_US 80000U
#endif

// Guards against the open-then-close-on-same-touch flicker (#909).
#define SETTINGS_OPEN_TAP_GUARD_MS 300

// Draw-buffer line count derived in display_driver.cpp::computeLvglBufLines.

// Below this, refuse FS open to keep newlib __sfp out of abort() (#651).
// Lowered 512→256 after CAN_NO_MEM at boot starved icon preload — abort
// threshold sits around 200 B internal-cap free, 256 keeps a margin.
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

// EMA weight applied to gauge widgets only — label/warning use raw values.
#define SIGNAL_EMA_ALPHA 0.2f

// Single source of truth across parser, TX, and JSON validation (F-LO-3).
#ifdef __cplusplus
static constexpr uint8_t kCanFrameMaxBytes = 8;
#else
    #define CAN_FRAME_MAX_BYTES 8U
#endif

#define CAN_RX_QUEUE_DEPTH 32

// Yields so IDLE0 can feed the WDT — twai_receive returns immediately on busy bus (#200).
#define CAN_TASK_YIELD_TICKS 1

// TWAI driver retries init on a timer instead of spamming INVALID_STATE (#652).
#define TWAI_INIT_RETRY_MS 5000U
#define TWAI_INIT_MAX_RETRIES 6U

// v1 single-ECU path: 0x7DF request, 0x7E8 response (ECM).
// Multi-ECU + ISO-TP deferred (#841).
#define OBD2_REQUEST_FRAME_ID 0x7DFU
#define OBD2_RESPONSE_FRAME_ID 0x7E8U

// Mirror of canshift-core OBD2_{MIN,MAX}_INTERVAL_MS.
#define OBD2_MIN_INTERVAL_MS_FW 100U
#define OBD2_MAX_INTERVAL_MS_FW 60000U

#define OBD2_MAX_POLL_SLOTS CONFIG_MAX_SIGNALS

// dashboard.json doubles as the USB rxBuf for CMD_PUT_CONFIG — real burns at
// v1.11 land ~13 KB, so 16 KB clears the overflow.
#define CONFIG_JSON_DOC_DASHBOARD 16384
#define CONFIG_JSON_DOC_SIGNALS 4096

// Static BSS arrays. 4→8 OOM'd CAN+USB init (+25 KB); 5 fits demo + cruise (#1357/#1360).
// Heap-allocating the page array would retire this static cap (#1359).
#define CONFIG_MAX_PAGES 5
#define CONFIG_MAX_WIDGETS_PER_PAGE 12
#define CONFIG_MAX_SIGNALS 32

// Mirrors canshift-core MAX_RAMP_STOPS (#430).
#define CFG_MAX_RAMP_STOPS 8

// Writes firmware-baked defaults on first boot when target is missing (#173).
#ifndef DEFAULT_CONFIG_PROVISION_ENABLED
    #define DEFAULT_CONFIG_PROVISION_ENABLED 1
#endif

// Raising widens FwError[] copies inside critical sections — keep ≤16 (F-ME-12).
#define ERROR_STORE_RING_SIZE 6U

#define ALERT_REVLIMIT_WARN_PCT 95
#define ALERT_REVLIMIT_FLASH_PCT 100
#define ALERT_REVLIMIT_FLASH_HZ 8

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
// WiFi OTA — 0 drops WiFi/WebServer/Update libs (~80 KB flash) (#48).
#ifndef APP_WIFI_OTA_ENABLED
    #define APP_WIFI_OTA_ENABLED 0
#endif

// Requires APP_WIFI_OTA_ENABLED=1 to have a WebServer (#1077).
#ifndef APP_SPA_SERVE
    #define APP_SPA_SERVE 0
#endif

#ifndef TASK_STACK_BLE
    #define TASK_STACK_BLE 5120
#endif
#define TASK_PRIO_BLE 6
#define TASK_CORE_BLE 1

#ifndef TASK_STACK_WIFI
    #define TASK_STACK_WIFI 4096
#endif
#define TASK_PRIO_WIFI 5
#define TASK_CORE_WIFI 1

#ifndef TASK_STACK_WIFI_TCP
    #define TASK_STACK_WIFI_TCP 4096
#endif
#define TASK_PRIO_WIFI_TCP 5
#define TASK_CORE_WIFI_TCP 1

#ifndef TASK_STACK_WIFI_WS
    #define TASK_STACK_WIFI_WS 4096
#endif
#define TASK_PRIO_WIFI_WS 5
#define TASK_CORE_WIFI_WS 1

#define BLE_TELE_INTERVAL_MS 100

// Pre-init guard for NimBLE + advertising; GATT guard covers server + 4 chars (#909).
#define BLE_MIN_HEAP_BYTES (50U * 1024U)
#define BLE_GATT_MIN_HEAP_BYTES (24U * 1024U)

#define BLE_WIFI_AP_TIMEOUT_MS (5UL * 60UL * 1000UL)

// HMAC-SHA256 trailer required on every OTA upload (#205, #667).
#ifndef APP_OTA_REQUIRE_HMAC
    #define APP_OTA_REQUIRE_HMAC 1
#endif

// Injected via secrets.ini (gitignored). Replace before prod (#205).
#ifndef OTA_HMAC_SECRET
    #define OTA_HMAC_SECRET "DEV_INSECURE_REPLACE_BEFORE_PROD"
#endif

// PUT_CONFIG wraps dashboard JSON + ~256 B envelope.
#define USB_RX_BUF_SIZE (CONFIG_JSON_DOC_DASHBOARD + 256)

// v2: LOG_* emit {"log":1,...} envelopes; UART0 + wire share a mutex (#199).
#define USB_PROTOCOL_VERSION 2

// Long enough for the driver to glance; short enough not to hide the dash (#189).
#define BURN_OVERLAY_ERROR_HOLD_MS 3000

// 0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose. Release stays at
// `error` — info previously leaked rpm/throttle/lambda over UART (#899).
#ifndef APP_LOG_LEVEL
    #if APP_DEBUG_BUILD
        #define APP_LOG_LEVEL 4
    #else
        #define APP_LOG_LEVEL 1
    #endif
#endif

#define LOG_TAG_MAX_LEN 16

#ifndef APP_LV_TASK_LOG
    #define APP_LV_TASK_LOG 0
#endif

// Off-by-default — LOG_VDEBUG sites collapse at preprocess time even if level≥4.
#ifndef APP_VERBOSE_DEBUG_LOGS
    #if APP_DEBUG_BUILD
        #define APP_VERBOSE_DEBUG_LOGS 1
    #else
        #define APP_VERBOSE_DEBUG_LOGS 0
    #endif
#endif
