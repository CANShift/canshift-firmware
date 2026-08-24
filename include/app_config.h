#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef APP_VERSION_STR
    #define APP_VERSION_STR "0.0.0-unset"
#endif

#ifndef APP_DEBUG_BUILD
    #define APP_DEBUG_BUILD 0
#endif

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
inline constexpr int TASK_PRIO_UI = 10;
inline constexpr int TASK_PRIO_CAN = 15;
inline constexpr int TASK_PRIO_USB = 8;
inline constexpr int TASK_PRIO_INPUT = 7;

inline constexpr int TASK_CORE_UI = 1;
inline constexpr int TASK_CORE_CAN = 0;
inline constexpr int TASK_CORE_USB = 1;

inline constexpr int TASK_CORE_INPUT = 0;

#ifndef TASK_STACK_INPUT
    #define TASK_STACK_INPUT 2048
#endif

inline constexpr unsigned TASK_WDT_TIMEOUT_MS = 8000U;

inline constexpr unsigned PRE_RESTART_FLUSH_DELAY_MS = 200U;

#ifndef APP_USB_TICK_TRACE
    #define APP_USB_TICK_TRACE 0
#endif

inline constexpr int USB_TICK_DURATION_WARN_US = 1000000UL;

inline constexpr int USB_TICK_INTERVAL_WARN_US = 200000UL;

#ifndef APP_USB_CAN_SCAN_FAIL_LOUD
    #define APP_USB_CAN_SCAN_FAIL_LOUD 0
#endif

inline constexpr int LVGL_TICK_MS = 5;

inline constexpr int LVGL_HANDLER_PERIOD_MS = 10;

inline constexpr int SWIPE_CANCEL_THRESHOLD_PX = 8;

inline constexpr int BUTTON_SIGNAL_SYNC_GRACE_MS = 500;

inline constexpr int BUTTON_COMMAND_TIMEOUT_MS = 15000;

#ifndef APP_TOUCH_LATENCY_WARN_US
    #define APP_TOUCH_LATENCY_WARN_US 80000U
#endif

inline constexpr int SETTINGS_OPEN_TAP_GUARD_MS = 300;

inline constexpr size_t LVGL_FS_MIN_HEAP_BYTES = 256;

inline constexpr int SIGNAL_STORE_MAX_SIGNALS = 64;

inline constexpr int SIGNAL_DEFAULT_TIMEOUT_MS = 500;

inline constexpr float SIGNAL_EMA_ALPHA = 0.2f;

inline constexpr uint8_t kCanFrameMaxBytes = 8;

inline constexpr int CAN_RX_QUEUE_DEPTH = 32;

inline constexpr int CAN_TASK_YIELD_TICKS = 1;

inline constexpr unsigned TWAI_INIT_RETRY_MS = 5000U;
inline constexpr unsigned TWAI_INIT_MAX_RETRIES = 6U;

inline constexpr unsigned OBD2_REQUEST_FRAME_ID = 0x7DFU;
inline constexpr unsigned OBD2_RESPONSE_FRAME_ID = 0x7E8U;

inline constexpr unsigned OBD2_MIN_INTERVAL_MS_FW = 100U;
inline constexpr unsigned OBD2_MAX_INTERVAL_MS_FW = 60000U;

inline constexpr int CONFIG_JSON_DOC_DASHBOARD = 16384;

inline constexpr int CONFIG_MAX_PAGES = 8;
inline constexpr int CONFIG_MAX_WIDGETS_PER_PAGE = 12;
inline constexpr unsigned CONFIG_DASHBOARD_HEAP_BUDGET_BYTES = (48u * 1024u);
inline constexpr int CONFIG_MAX_SIGNALS = 48;

inline constexpr int OBD2_MAX_POLL_SLOTS = CONFIG_MAX_SIGNALS;

#ifndef DEFAULT_CONFIG_PROVISION_ENABLED
    #define DEFAULT_CONFIG_PROVISION_ENABLED 1
#endif

inline constexpr unsigned TRACK_TELEMETRY_TIMEOUT_MS = 5000U;

inline constexpr unsigned DISPLAY_DIM_AFTER_IDLE_MS = 60000U;
inline constexpr unsigned DISPLAY_OFF_AFTER_IDLE_MS = 600000U;
inline constexpr unsigned DISPLAY_DIM_PERCENT = 20U;

inline constexpr unsigned ERROR_STORE_RING_SIZE = 6U;

inline constexpr int ALERT_REVLIMIT_WARN_PCT = 95;
inline constexpr int ALERT_REVLIMIT_FLASH_PCT = 100;
/* 6 Hz is the design spec. It exceeds WCAG 2.3.1's 3-flash limit, which guarded the
   full-screen border this replaced; the strip and one numeral are far under the
   25%-of-viewport area the threshold applies to. Drop to 3 to restore the old margin. */
inline constexpr int ALERT_REVLIMIT_FLASH_HZ = 6;

inline constexpr float ALERT_HYSTERESIS_PCT = 2.0f;
inline constexpr unsigned ALERT_MIN_ACTIVE_MS = 2000U;

inline constexpr unsigned ALERT_SENSOR_LOST_CLEAR_HOLD_MS = 3000U;

inline constexpr float BATTERY_DEFAULT_LOW_WARN_V = 12.0f;
inline constexpr float BATTERY_DEFAULT_LOW_CRIT_V = 11.5f;
inline constexpr float BATTERY_DEFAULT_HIGH_WARN_V = 15.0f;
inline constexpr float BATTERY_DEFAULT_HIGH_CRIT_V = 16.0f;

#ifndef APP_BLE_ENABLED
    #define APP_BLE_ENABLED 1
#endif

#ifndef TASK_STACK_BLE
    #define TASK_STACK_BLE 5120
#endif
inline constexpr int TASK_PRIO_BLE = 6;
inline constexpr int TASK_CORE_BLE = 1;

inline constexpr int BLE_TELE_INTERVAL_MS = 100;

inline constexpr unsigned BLE_MIN_HEAP_BYTES = (50U * 1024U);
inline constexpr unsigned BLE_GATT_MIN_HEAP_BYTES = (24U * 1024U);

inline constexpr int USB_RX_BUF_SIZE = (CONFIG_JSON_DOC_DASHBOARD + 256);

inline constexpr int USB_PROTOCOL_VERSION = 2;

inline constexpr int USB_SCREEN_SETTINGS_MUTEX_TIMEOUT_MS = 50;
inline constexpr int USB_PUT_CONFIG_MUTEX_TIMEOUT_MS = 100;
inline constexpr int USB_PRE_RESTART_FLUSH_DELAY_MS = 250;

inline constexpr int USB_TX_LOCK_TIMEOUT_MS = 1000;

inline constexpr int USB_TX_WRITE_TIMEOUT_MS = 2000;

inline constexpr int USB_TX_BUFFER_BYTES = 4096;

inline constexpr int USB_TX_PIECE_BYTES = 64;

inline constexpr int USB_RX_LINE_TIMEOUT_MS = 2000;
inline constexpr int OTA_COMPLETE_SCREEN_HOLD_MS = 1200;
inline constexpr int USB_TASK_TICK_INTERVAL_MS = 20;

inline constexpr int BURN_OVERLAY_ERROR_HOLD_MS = 3000;

#ifndef APP_LOG_LEVEL
    #if APP_DEBUG_BUILD
        #define APP_LOG_LEVEL 4
    #else
        #define APP_LOG_LEVEL 1
    #endif
#endif

inline constexpr int LOG_TAG_MAX_LEN = 16;

#ifndef APP_LV_TASK_LOG
    #define APP_LV_TASK_LOG 0
#endif

#ifndef APP_VERBOSE_DEBUG_LOGS
    #if APP_DEBUG_BUILD
        #define APP_VERBOSE_DEBUG_LOGS 1
    #else
        #define APP_VERBOSE_DEBUG_LOGS 0
    #endif
#endif
