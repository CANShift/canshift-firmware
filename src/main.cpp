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

#include <esp_task_wdt.h>

#include "boot/boot_sequence.h"
#include "can/can_manager.h"
#include "diag/logger.h"
#include "diag/lvgl_lock_guard.h"
#include "diag/perf_counters.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/usb/usb_comm.h"
#include "runtime/alert_engine.h"
#include "runtime/input_buttons.h"
#include "runtime/pending_actions.h"
#include "ui/burn_overlay.h"
#include "ui/page_manager.h"
#include "ui/passkey_overlay.h"
#include "ui/theme_manager.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
    #include "hal/wifi/wifi_ap.h"
#endif
// Task function forward declarations
void taskUI(void *pvParameters);
void taskCAN(void *pvParameters);
void taskUSBComm(void *pvParameters);
#if APP_BLE_ENABLED
void taskBLE(void *pvParameters);
#endif

// ---------------------------------------------------------------------------
// Global LVGL mutex
// LVGL is NOT thread-safe. All LVGL calls from any task must hold this mutex.
// ---------------------------------------------------------------------------
SemaphoreHandle_t g_lvglMutex = nullptr;

// ---------------------------------------------------------------------------
// UI task handle — exposed so transports (USB, BLE) can xTaskNotifyGive() it
// from their pending-action publishers. Used by the BurnOverlay observer
// wired in setup() to wake the UI task ahead of its next 10 ms tick when a
// CMD_PUT_CONFIG dispatch needs the overlay painted before the flash write.
// Single writer (setup), many readers — pointer-sized so the unguarded read
// is torn-free on ESP32. Null until createAllTasks() has run.
// ---------------------------------------------------------------------------
TaskHandle_t g_uiTaskHandle = nullptr;

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
        // vTaskDelay explicitly yields to IDLE so the WDT keeps running on
        // this halted task — no reliance on the Arduino delay() shim (#1207).
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// ---------------------------------------------------------------------------
// BurnOverlay observer (#1207 #1314) — wired into the USB transport via
// UsbComm::setBurnOverlayShow{,Error}Callback after createAllTasks() so
// the UI task handle published in g_uiTaskHandle is in scope. The callbacks
// publish a PendingActions flag and notify the UI task; the UI task drains
// the flag inside its LVGL-mutex window and calls BurnOverlay::show() at
// TASK_PRIO_UI — no priority flip on the USB task.
// ---------------------------------------------------------------------------

static void burnOverlayShowObserver() {
    PendingActions::burnOverlayShow.store(true, std::memory_order_relaxed);
    // Wake the UI task ahead of its next LVGL_HANDLER_PERIOD_MS deadline.
    // xTaskNotifyGive is ISR-safe and never blocks; safe to call from any
    // task including a transport task holding no LVGL state.
    if (g_uiTaskHandle != nullptr) {
        xTaskNotifyGive(g_uiTaskHandle);
    }
}

static void burnOverlayShowErrorObserver(int reason) {
    PendingActions::burnOverlayShowError.store(static_cast<int8_t>(reason),
                                               std::memory_order_relaxed);
    if (g_uiTaskHandle != nullptr) {
        xTaskNotifyGive(g_uiTaskHandle);
    }
}

static void registerBurnOverlayObserver() {
    UsbComm::setBurnOverlayShowCallback(&burnOverlayShowObserver);
    UsbComm::setBurnOverlayShowErrorCallback(&burnOverlayShowErrorObserver);
}

// ---------------------------------------------------------------------------
// Create all FreeRTOS tasks using the pre-allocated static stacks and TCBs.
// xTaskCreateStaticPinnedToCore never allocates from the heap, so it always
// succeeds here even with the fragmented post-lv_init() heap.
//
// Each "hot loop" task (UI/CAN/USB/BLE) is also subscribed to the Task
// Watchdog Timer that was armed in BootSequence::run(). Its tick body calls
// esp_task_wdt_reset() once per iteration — a hang in any of those loops
// longer than TASK_WDT_TIMEOUT_MS fires the panic handler and the device
// auto-resets (issue #666, BLE added in #1006). The WiFi AP task is created
// on-demand from WifiAp::start() and self-registers from inside its task
// body — see hal/wifi/wifi_ap.cpp.
// ---------------------------------------------------------------------------
static void createAllTasks() {
    TaskHandle_t uiHandle = xTaskCreateStaticPinnedToCore(
        taskUI, "ui", TASK_STACK_UI, nullptr, TASK_PRIO_UI, s_uiStack, &s_uiTaskTCB, TASK_CORE_UI);
    // Publish the UI task handle so the BurnOverlay observer (registered in
    // setup() below) can xTaskNotifyGive() it ahead of the regular tick
    // cadence — see g_uiTaskHandle decl.
    g_uiTaskHandle = uiHandle;

    TaskHandle_t canHandle =
        xTaskCreateStaticPinnedToCore(taskCAN, "can", TASK_STACK_CAN, nullptr, TASK_PRIO_CAN,
                                      s_canStack, &s_canTaskTCB, TASK_CORE_CAN);

    TaskHandle_t usbHandle =
        xTaskCreateStaticPinnedToCore(taskUSBComm, "usb", TASK_STACK_USB, nullptr, TASK_PRIO_USB,
                                      s_usbStack, &s_usbTaskTCB, TASK_CORE_USB);

#if APP_BLE_ENABLED
    TaskHandle_t bleHandle =
        xTaskCreateStaticPinnedToCore(taskBLE, "ble", TASK_STACK_BLE, nullptr, TASK_PRIO_BLE,
                                      s_bleStack, &s_bleTaskTCB, TASK_CORE_BLE);
#endif

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
#if APP_BLE_ENABLED
    // Issue #1006 — subscribe taskBLE to the WDT. The task loops every
    // BLE_TELE_INTERVAL_MS (100 ms) and its body is non-blocking: pairing
    // crypto and GATT discovery run on NimBLE's own host task, not here.
    // Worst case in this task is BleServer::stop() → NimBLEDevice::deinit(),
    // bounded well under TASK_WDT_TIMEOUT_MS (8 s).
    if (bleHandle) {
        const esp_err_t err = esp_task_wdt_add(bleHandle);
        if (err != ESP_OK)
            LOG_WARN("BOOT", "WDT add(ble) failed: %d", static_cast<int>(err));
    }
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

    g_lvglMutex = xSemaphoreCreateMutex();
    if (!g_lvglMutex) {
        LOG_ERROR("BOOT", "Failed to create LVGL mutex — halting");
        // vTaskDelay explicitly yields to IDLE so the WDT keeps running on
        // this halted task — no reliance on the Arduino delay() shim (#1207).
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Must run before BootSequence::run() — see preallocateTaskStacks().
    preallocateTaskStacks();

    // USB rxBuf is allocated lazily inside UsbComm::init() (post-lv_init).
    // On heap-tight WROOM boots (missing SPIFFS → font/image cache frag),
    // letting BLE / WiFi grab their working memory FIRST and treating USB
    // CDC config push as best-effort lets the device boot every time.
    // If the late alloc fails, reserveRxBuf() logs and disables USB silently;
    // recovery via BLE pairing or WiFi AP toggle is still possible.

    // Runs synchronous boot: HAL init → lv_init() → load config → build UI.
    BootSequence::run();

    // Start tick timer only after lv_init() has run inside BootSequence.
    startLvglTickTimer();

    // No-op when APP_PROFILE_UI=0.
    PERF_INIT();

    LOG_INFO("BOOT", "Boot complete — starting tasks");
    createAllTasks();
    // BurnOverlay observer wiring (#1207 #1314) — must run AFTER createAllTasks()
    // so g_uiTaskHandle is non-null. Until this call, UsbCommInternal::invoke*
    // is a no-op so any CMD_PUT_CONFIG arriving in the window above proceeds
    // without visual feedback but still completes the storage write (no race —
    // the USB task itself isn't running until createAllTasks() returned).
    registerBurnOverlayObserver();
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

// Number of consecutive successful UI frames (mutex acquired + LVGL handler
// returned) before the OTA slot is marked valid. The mark cancels the
// bootloader's pending rollback, so deferring it until paint has actually
// succeeded N times catches first-frame failures (font decode panic, theme
// apply, page rebuild) that the old "fire from BootSequence" placement
// missed. 30 frames at LVGL_HANDLER_PERIOD_MS=10 (target 100 Hz) is ~300 ms
// in steady state, and ~3 s even under sustained 10 FPS load — comfortably
// inside the bootloader's rollback window yet wide enough that a transient
// first-paint glitch still trips it. F-ME-8 / issue #1014.
static constexpr int UI_OTA_VALID_FRAMES = 30;

// ---------------------------------------------------------------------------
// taskUI per-frame phase helpers
//
// Each helper covers one discrete phase of the UI loop iteration. They are
// kept in an anonymous namespace and inlined-by-default so the resulting
// binary is byte-identical to the inlined form, and so the orchestrator stays
// a ≤ 40 line loop body (CLAUDE.md function-length cap, issue #1014).
//
// Ordering contract — the orchestrator MUST call them in this order so the
// behaviour matches the pre-split code:
//   1. uiDrainPreMutexActions     (touch calibrate / calibration reset)
//   2. uiAcquireLvglMutex         (timed take + perf sample)
//   3. uiRunMutexBody             (touch poll → day/night → passkey → LVGL)
//   4. uiHandleOtaMark            (one-shot OTA slot validation)
//   5. uiFeedTaskWdt              (#666 — must run every iteration)
//   6. uiNotifyDayNightChanged    (post-mutex BLE STATUS notify)
//   7. uiRecordFrameMetrics       (frame total + frame-miss)
//   8. uiFlushLvTaskLog           (1 Hz LVGL handler stat log)
//   9. uiThrottle                 (vTaskDelayUntil + #976 yield-guard)
// ---------------------------------------------------------------------------

namespace {

// 1. Pre-mutex action drain. Touch calibrate blocks the screen with crosshairs
// and draws via TFT_eSPI directly, so it MUST run without g_lvglMutex held.
// Both calibration entry points (BLE + USB) feed the same flag (#893); we
// drain it once here. Calibration-reset only touches NVS but stays on the UI
// task to keep all NVS writes single-threaded.
inline void uiDrainPreMutexActions() {
    if (PendingActions::takeTouchCalibrate()) {
        TouchDriver::calibrate();
#if APP_BLE_ENABLED
        // Push a STATUS notify so a connected BLE client sees the result —
        // matters even when the trigger came over USB.
        BleServer::pushStatusNotify();
#endif
    }
    if (PendingActions::takeTouchCalibrationReset()) {
        TouchDriver::resetCalibration();
    }
}

// 2. Timed mutex acquire. The 10 ms timeout is the same as pre-split; the
// MUTEX_WAIT perf sample is recorded for both success and timeout paths so
// the histogram still captures starvation events.
inline BaseType_t uiAcquireLvglMutex() {
#if APP_PROFILE_UI
    const int64_t lockStartUs = esp_timer_get_time();
#endif
    const BaseType_t mutexTaken = xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(10));
#if APP_PROFILE_UI
    ::PerfCounters::recordSample(::PerfCounters::MUTEX_WAIT,
                                 static_cast<uint32_t>(esp_timer_get_time() - lockStartUs));
#endif
    return mutexTaken;
}

// 2a. Day/night theme drain — runs under g_lvglMutex (called from
// uiRunMutexBody). Explicit set wins over toggle when both are pending in the
// same tick (#893). Returns true when the resolved mode actually changed so
// the caller can fire the BLE STATUS notify after the mutex is released.
inline bool uiDrainDayNightActions() {
    const bool prevIsDay = ThemeManager::isDayMode();

    const int8_t dnSet = PendingActions::takeDayNightSet();
    if (dnSet >= 0) {
        ThemeManager::setDayMode(dnSet == 1);
        // Drop any stale toggle to avoid undoing the explicit set.
        (void)PendingActions::takeDayNightToggle();
    } else if (PendingActions::takeDayNightToggle()) {
        ThemeManager::toggleDayMode();
    }

    return ThemeManager::isDayMode() != prevIsDay;
}

// 2b. BLE passkey overlay drain — runs under g_lvglMutex. Hide is processed
// first so a quick disconnect-then-reconnect within one tick ends up showing
// the *new* code rather than tearing down the overlay the BLE task just
// asked us to put up (issue #873).
inline void uiDrainPasskeyActions() {
    if (PendingActions::takeBlePasskeyHide()) {
        PasskeyOverlay::hide();
    }
    const uint32_t passkey = PendingActions::takeBlePasskeyShow();
    if (passkey != 0u) {
        PasskeyOverlay::show(passkey);
    }
}

// 2b'. BurnOverlay drain (#1207 #1314) — runs under g_lvglMutex. The USB
// CDC dispatch path no longer paints the overlay inline on the USB task; it
// publishes a pending-action flag via the observer registered in setup()
// and notifies this task to render at TASK_PRIO_UI. The error path is
// drained AFTER the spinner show so a rapid show→error sequence collapses
// to a single error overlay (the show is no-oped — teardownOverlay() inside
// showError handles the previous state).
inline void uiDrainBurnOverlayActions() {
    if (PendingActions::takeBurnOverlayShow()) {
        BurnOverlay::show();
    }
    const int8_t err = PendingActions::takeBurnOverlayShowError();
    if (err >= 0) {
        BurnOverlay::showError(static_cast<BurnOverlay::ErrorReason>(err));
    }
}

// 2c. lv_task_handler() invocation with PERF_SCOPE and the APP_LV_TASK_LOG
// duration aggregator. Runs under g_lvglMutex.
inline void uiRunLvTaskHandler() {
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

// 3. Under-mutex body. lv_tick_inc() is driven by the esp_timer set up in
// setup() — keeping it out of this loop means animations stay wall-clock
// accurate even when the UI task overruns. The mutex is given back inside
// this helper so the LVGL_HOLD_GUARD scope ends together with the LVGL work
// (same lifetime as pre-split). Returns true when the day/night mode flipped
// so the orchestrator can defer the BLE STATUS notify until after release.
inline bool uiRunMutexBody() {
    LVGL_HOLD_GUARD(::PerfCounters::MUTEX_HOLD_UI);
    TouchDriver::poll();
    const bool didDayNightChange = uiDrainDayNightActions();
    uiDrainPasskeyActions();
    uiDrainBurnOverlayActions();
    PageManager::updateWidgets();
    uiRunLvTaskHandler();
    xSemaphoreGive(g_lvglMutex);
    return didDayNightChange;
}

// 4. OTA rollback cancel — fires exactly once per boot, after
// UI_OTA_VALID_FRAMES healthy frames. Compile-time no-op in sim builds.
// F-ME-8 / issue #674 / #1014. The counter saturates at the threshold so it
// cannot wrap on a long-lived device, and `otaSlotMarked` latches so the
// call fires exactly once regardless of partition transitions afterwards.
inline void uiHandleOtaMark(int &successfulFrames, bool &otaSlotMarked) {
    if (successfulFrames < UI_OTA_VALID_FRAMES) {
        ++successfulFrames;
    }
    if (!otaSlotMarked && successfulFrames >= UI_OTA_VALID_FRAMES) {
        BootSequence::markOtaSlotValidIfPending();
        otaSlotMarked = true;
    }
}

// 5. Issue #666 — feed the Task WDT once per UI tick. Placed after
// lv_task_handler() (the slowest leg of the loop) so genuine LVGL deadlocks
// deeper in the iteration still trip the watchdog.
inline void uiFeedTaskWdt() {
    esp_task_wdt_reset();
}

// 5b. WiFi AP deferred auto-start (issue #1263). Polls the WifiAp module
// outside the LVGL mutex so the eventual start() call (which spawns a
// FreeRTOS task on core 1) doesn't fight UI work for the lock. The function
// rate-limits itself internally so this runs at most once per second's worth
// of UI ticks and is a few branches when nothing needs doing.
inline void uiTickWifiApAutoStart() {
#if APP_BLE_ENABLED
    WifiAp::tickAutoStart();
#endif
}

// 6. Post-mutex day/night BLE STATUS notify. Deferred until after the mutex
// is released so ThemeManager::isDayMode() is stable when the BLE task reads
// it through the notify pipeline.
inline void uiNotifyDayNightChanged(bool didDayNightChange) {
#if APP_BLE_ENABLED
    if (didDayNightChange) {
        BleServer::pushStatusNotify();
    }
#else
    (void)didDayNightChange;
#endif
}

// 7. Per-frame perf metrics. Frame-total wall time is captured before the
// delay so the metric measures useful work, not the deliberate sleep. The
// frame-miss heuristic compares lastWake deltas against the configured
// period: >2 ms over budget = a missed deadline.
inline void uiRecordFrameMetrics(int64_t frameStartUs, TickType_t lastWake) {
#if APP_PROFILE_UI
    const int64_t frameEndUs = esp_timer_get_time();
    ::PerfCounters::recordSample(::PerfCounters::FRAME_TOTAL,
                                 static_cast<uint32_t>(frameEndUs - frameStartUs));
    const TickType_t nowTicks = xTaskGetTickCount();
    if ((nowTicks - lastWake) > pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS + 2)) {
        ::PerfCounters::recordFrameMiss();
    }
    ::PerfCounters::tick();
#else
    (void)frameStartUs;
    (void)lastWake;
#endif
}

// 8. 1 Hz LVGL handler stat log flush. Aggregates avg/max/count of
// lv_task_handler() duration over the last second and emits one INFO line.
inline void uiFlushLvTaskLog() {
#if APP_LV_TASK_LOG
    const int64_t _now = esp_timer_get_time();
    if (s_lvLastFlushUs == 0)
        s_lvLastFlushUs = _now;
    if (_now - s_lvLastFlushUs >= 1000000) {
        const uint32_t avg = s_lvCount ? (s_lvSumUs / s_lvCount) : 0;
        LOG_INFO("PERF", "lv_task: avg=%uus max=%uus n=%u", avg, static_cast<unsigned>(s_lvMaxUs),
                 static_cast<unsigned>(s_lvCount));
        s_lvSumUs = 0;
        s_lvMaxUs = 0;
        s_lvCount = 0;
        s_lvLastFlushUs = _now;
    }
#endif
}

// 9. Frame throttle. Sustained LVGL frame overruns (heavy page rebuild, icon
// decode) would otherwise pin the UI task to CPU 1: vTaskDelayUntil returns
// immediately once the next wake-up is already past, the UI task (prio 10)
// keeps running, and lower-priority tasks on the same core (USB prio 8,
// BLE prio 6, Sim prio 5) never get scheduled — the USB task then misses
// its WDT feed and the panic handler reboots the device with "usb (CPU 1)".
// Force a 1-tick yield when we'd have returned immediately (issue #976).
//
// Notify-aware wait (#1207 #1314): swap xTaskDelayUntil for
// ulTaskNotifyTake(pdTRUE, period) so a transport (USB CDC PUT_CONFIG,
// future BLE/WS render requests) can xTaskNotifyGive(g_uiTaskHandle) and
// wake this task ahead of the next LVGL_HANDLER_PERIOD_MS deadline. The
// ESP-IDF ulTaskNotifyTake returns either the (now-cleared) notification
// count, or 0 on timeout — both mean "tick the UI now". The deadline-anchor
// behaviour of xTaskDelayUntil is replaced with `lastWake = xTaskGetTickCount()`
// at the bottom; in steady state (no notifications) the frame cadence is
// identical to the previous pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS) wait.
//
// Issue #976 overrun guard is preserved: if a frame body overruns the period
// the notify-take returns immediately with timeout=0, so we still force a
// 1-tick yield instead of busy-spinning.
inline void uiThrottle(TickType_t &lastWake) {
    const TickType_t period = pdMS_TO_TICKS(LVGL_HANDLER_PERIOD_MS);
    const TickType_t now = xTaskGetTickCount();
    const TickType_t elapsed = now - lastWake;
    if (elapsed >= period) {
        // Frame body overran the period — yield one tick to lower-prio
        // tasks (#976) instead of waiting on a notification that may
        // never come and re-running back-to-back.
        vTaskDelay(1);
    } else {
        // Wait up to the remaining slice of the period for either a notify
        // (transport requesting an early render) or the deadline. pdTRUE
        // clears the notification count on take so back-to-back notifies
        // coalesce into one render.
        (void)ulTaskNotifyTake(pdTRUE, period - elapsed);
    }
    lastWake = xTaskGetTickCount();
}

} // namespace

void taskUI(void *pvParameters) {
    TickType_t lastWake = xTaskGetTickCount();
    // Counts UI frames that completed under g_lvglMutex. otaSlotMarked latches
    // once BootSequence::markOtaSlotValidIfPending() has fired so the call
    // happens exactly once per boot regardless of partition transitions.
    int successfulFrames = 0;
    bool otaSlotMarked = false;

    while (true) {
#if APP_PROFILE_UI
        const int64_t frameStartUs = esp_timer_get_time();
#else
        const int64_t frameStartUs = 0;
#endif
        const TickType_t frameWakeStart = lastWake;

        uiDrainPreMutexActions();

        bool didDayNightChange = false;
        if (uiAcquireLvglMutex() != pdTRUE) {
            LOG_WARN("UI", "LVGL mutex timeout — skipping update");
        } else {
            didDayNightChange = uiRunMutexBody();
            uiHandleOtaMark(successfulFrames, otaSlotMarked);
        }

        uiFeedTaskWdt();
        uiTickWifiApAutoStart();
        uiNotifyDayNightChanged(didDayNightChange);
        uiRecordFrameMetrics(frameStartUs, frameWakeStart);
        uiFlushLvTaskLog();
        uiThrottle(lastWake);
    }
}

// ---------------------------------------------------------------------------
// CAN task
// ---------------------------------------------------------------------------

void taskCAN(void *pvParameters) {
    // CanManager::initHardware() is called from BootSequence::run() before tasks start.
    // This task only runs the receive/dispatch loop.
    //
    // Yield strategy (issue #1258, refines #200):
    //   - Empty RX queue → `twai_receive` already blocked ~10 ms, which is
    //     plenty for IDLE0; skip the per-iteration vTaskDelay so the next
    //     iteration starts immediately when a frame finally arrives.
    //   - Frame consumed → keep firing through the queue without sleeping
    //     between reads (the previous unconditional vTaskDelay(1) capped the
    //     CAN task at ~1000 frames/s, which a busy MaxxECU bus exceeds).
    //     Force IDLE0 a slot every CAN_BURST_BEFORE_YIELD consecutive frames
    //     so the lower-priority IDLE task on this core still runs and the
    //     OS WDT stays fed (the original #200 invariant). At 800 frames/s
    //     with N=16 that's ~50 forced yields/s = 50 ms/s, ~5 % of the core.
    constexpr uint32_t CAN_BURST_BEFORE_YIELD = 16;
    uint32_t consecutiveFrames = 0;
    while (true) {
        const bool frameConsumed = CanManager::tick();
        // Issue #666 — CAN task WDT feed. CanManager::tick() blocks up to
        // 10 ms in twai_receive and may sleep 100 ms while retrying install;
        // both are well within TASK_WDT_TIMEOUT_MS.
        esp_task_wdt_reset();
        if (!frameConsumed) {
            consecutiveFrames = 0;
            continue;
        }
        if (++consecutiveFrames >= CAN_BURST_BEFORE_YIELD) {
            consecutiveFrames = 0;
            vTaskDelay(CAN_TASK_YIELD_TICKS);
        }
    }
}

// ---------------------------------------------------------------------------
// USB communication task
// ---------------------------------------------------------------------------

void taskUSBComm(void *pvParameters) {
    UsbComm::init();

#if APP_USB_TICK_TRACE
    // Per-iteration timestamps for the #976 trace: previous tick entry time
    // (for the inter-tick gap) and an iteration counter so log lines can be
    // correlated with the serial timeline.
    int64_t prevEntryUs = esp_timer_get_time();
    uint32_t tickCount = 0;
#endif

    while (true) {
#if APP_USB_TICK_TRACE
        const int64_t entryUs = esp_timer_get_time();
        const int64_t gapUs = entryUs - prevEntryUs;
        if (gapUs > static_cast<int64_t>(USB_TICK_INTERVAL_WARN_US)) {
            LOG_WARN("USB_TRACE", "tick gap %lld us (count=%lu) — USB task starved",
                     static_cast<long long>(gapUs), static_cast<unsigned long>(tickCount));
        }
        prevEntryUs = entryUs;
        const int64_t bodyStartUs = entryUs;
#endif

        UsbComm::tick();

#if APP_USB_TICK_TRACE
        const int64_t bodyDurUs = esp_timer_get_time() - bodyStartUs;
        if (bodyDurUs > static_cast<int64_t>(USB_TICK_DURATION_WARN_US)) {
            LOG_WARN("USB_TRACE", "tick body %lld us (count=%lu) — slow path inside tick()",
                     static_cast<long long>(bodyDurUs), static_cast<unsigned long>(tickCount));
        }
        ++tickCount;
#endif

        // Issue #666 — USB task WDT feed. Default tick cadence is 20 ms,
        // worst case (CMD_PUT_CONFIG burn under LVGL mutex) ~200 ms, both
        // far below TASK_WDT_TIMEOUT_MS.
        esp_task_wdt_reset();
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

        // Issue #1006 — BLE task WDT feed. Placed AFTER tick() (and any
        // start/stop transition) so a real hang inside NimBLE's tick path
        // still trips the watchdog. The 100 ms cadence + non-blocking tick
        // body leaves ~80x headroom against TASK_WDT_TIMEOUT_MS.
        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BLE_TELE_INTERVAL_MS));
    }
}

#endif
