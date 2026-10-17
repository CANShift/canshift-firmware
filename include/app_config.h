#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef APP_VERSION_STR
    #define APP_VERSION_STR "0.0.0-unset"
#endif

#ifndef APP_DEBUG_BUILD
    #define APP_DEBUG_BUILD 0
#endif

#ifndef APP_OTA_ALLOW_UNSIGNED
    #define APP_OTA_ALLOW_UNSIGNED 0
#endif

#if APP_OTA_ALLOW_UNSIGNED && !APP_DEBUG_BUILD
    #error "APP_OTA_ALLOW_UNSIGNED is for debug builds only"
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
inline constexpr int kTaskPrioUi = 10;
inline constexpr int kTaskPrioCan = 15;
inline constexpr int kTaskPrioUsb = 8;
inline constexpr int kTaskPrioInput = 7;

inline constexpr int kTaskCoreUi = 1;
inline constexpr int kTaskCoreCan = 0;
inline constexpr int kTaskCoreUsb = 1;

inline constexpr int kTaskCoreInput = 0;

#ifndef TASK_STACK_INPUT
    #define TASK_STACK_INPUT 2048
#endif

inline constexpr unsigned kTaskWdtTimeoutMs = 8000U;

inline constexpr unsigned kPreRestartFlushDelayMs = 200U;

#ifndef APP_USB_TICK_TRACE
    #define APP_USB_TICK_TRACE 0
#endif

inline constexpr int kUsbTickDurationWarnUs = 1000000UL;

inline constexpr int kUsbTickIntervalWarnUs = 200000UL;

#ifndef APP_USB_CAN_SCAN_FAIL_LOUD
    #define APP_USB_CAN_SCAN_FAIL_LOUD 0
#endif

inline constexpr int kLvglHandlerPeriodMs = 10;

inline constexpr uint32_t kWidgetRefreshPeriodMs = 20;

inline constexpr int kSwipeCancelThresholdPx = 8;

inline constexpr int kButtonSignalSyncGraceMs = 500;

inline constexpr int kButtonCommandTimeoutMs = 15000;

#ifndef APP_TOUCH_LATENCY_WARN_US
    #define APP_TOUCH_LATENCY_WARN_US 80000U
#endif

inline constexpr int kSettingsOpenTapGuardMs = 300;

inline constexpr size_t kLvglFsMinHeapBytes = 256;

inline constexpr int kSignalStoreMaxSignals = 64;

inline constexpr int kSignalDefaultTimeoutMs = 500;

inline constexpr float kSignalEmaAlpha = 0.2f;

inline constexpr uint8_t kCanFrameMaxBytes = 8;

inline constexpr int kCanRxQueueDepth = 32;

inline constexpr int kCanTaskYieldTicks = 1;

inline constexpr unsigned kTwaiInitRetryMs = 5000U;
inline constexpr unsigned kTwaiInitMaxRetries = 6U;

inline constexpr unsigned kObd2RequestFrameId = 0x7DFU;
inline constexpr unsigned kObd2ResponseFrameId = 0x7E8U;

inline constexpr unsigned kObd2MinIntervalMsFw = 100U;
inline constexpr unsigned kObd2MaxIntervalMsFw = 60000U;

inline constexpr int kConfigJsonDocDashboard = 16384;

inline constexpr uint32_t kUsbPutConfigMaxBytes = 64u * 1024u;
inline constexpr uint32_t kUsbPutFileMaxBytes = 128u * 1024u;
inline constexpr uint32_t kUsbChunkIdleTimeoutMs = 10000u;

inline constexpr int kConfigMaxPages = 8;
inline constexpr int kConfigMaxWidgetsPerPage = 12;
inline constexpr unsigned kConfigDashboardHeapBudgetBytes = (48u * 1024u);
inline constexpr int kConfigMaxSignals = 48;

inline constexpr int kConfigMaxCriticalAlerts = 4;

inline constexpr int kObd2MaxPollSlots = kConfigMaxSignals;

#ifndef DEFAULT_CONFIG_PROVISION_ENABLED
    #define DEFAULT_CONFIG_PROVISION_ENABLED 1
#endif

inline constexpr unsigned kTrackTelemetryTimeoutMs = 5000U;

inline constexpr unsigned kDisplayDimAfterIdleMs = 60000U;
inline constexpr unsigned kDisplayOffAfterIdleMs = 600000U;
inline constexpr unsigned kDisplayDimPercent = 20U;

inline constexpr unsigned kErrorStoreRingSize = 6U;

inline constexpr int kAlertRevlimitWarnPct = 95;
inline constexpr int kAlertRevlimitFlashPct = 100;
/* 6 Hz is the design spec. It exceeds WCAG 2.3.1's 3-flash limit, which guarded the
   full-screen border this replaced; the strip and one numeral are far under the
   25%-of-viewport area the threshold applies to. Drop to 3 to restore the old margin. */
inline constexpr int kAlertRevlimitFlashHz = 6;

inline constexpr float kAlertHysteresisPct = 2.0f;
inline constexpr unsigned kAlertMinActiveMs = 2000U;

inline constexpr unsigned kAlertSensorLostClearHoldMs = 3000U;

inline constexpr float kBatteryDefaultLowWarnV = 12.0f;
inline constexpr float kBatteryDefaultLowCritV = 11.5f;
inline constexpr float kBatteryDefaultHighWarnV = 15.0f;
inline constexpr float kBatteryDefaultHighCritV = 16.0f;

#ifndef APP_BLE_ENABLED
    #define APP_BLE_ENABLED 1
#endif

#ifndef APP_WORKSHOP_ENABLED
    #define APP_WORKSHOP_ENABLED 0
#endif

#ifndef TASK_STACK_BLE
    #define TASK_STACK_BLE 5120
#endif
inline constexpr int kTaskPrioBle = 6;
inline constexpr int kTaskCoreBle = 1;

inline constexpr int kBleTeleIntervalMs = 100;

inline constexpr unsigned kBleMinHeapBytes = (50U * 1024U);
inline constexpr unsigned kBleGattMinHeapBytes = (24U * 1024U);

inline constexpr int kUsbRxBufSize = (kConfigJsonDocDashboard + 256);

inline constexpr int kUsbProtocolVersion = 2;

inline constexpr int kUsbScreenSettingsMutexTimeoutMs = 50;
inline constexpr int kUsbPutConfigMutexTimeoutMs = 100;
inline constexpr int kUsbPreRestartFlushDelayMs = 250;

inline constexpr int kUsbTxLockTimeoutMs = 1000;

inline constexpr int kUsbTxWriteTimeoutMs = 2000;

inline constexpr int kUsbTxBufferBytes = 4096;

inline constexpr int kUsbTxPieceBytes = 64;

inline constexpr int kUsbRxLineTimeoutMs = 2000;
inline constexpr int kOtaCompleteScreenHoldMs = 1200;
inline constexpr int kUsbTaskTickIntervalMs = 20;

inline constexpr int kBurnOverlayErrorHoldMs = 3000;

#ifndef APP_LOG_LEVEL
    #if APP_DEBUG_BUILD
        #define APP_LOG_LEVEL 4
    #else
        #define APP_LOG_LEVEL 1
    #endif
#endif

inline constexpr int kLogTagMaxLen = 16;

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
