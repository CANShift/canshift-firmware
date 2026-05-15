#pragma once
// ble_server.h — BLE GATT server for mobile app (CANShift Mobile)
//
// Service UUIDs (128-bit, CANShift vendor prefix 4fa0b6a0-*):
//   Service:   4fa0b6a0-0000-0000-0000-000000000001
//   TELE:      4fa0b6a0-0000-0000-0000-000000000002  notify — live signal JSON ~10Hz
//   STATUS:    4fa0b6a0-0000-0000-0000-000000000003  read  — firmware version + CAN health
//   SETTINGS:  4fa0b6a0-0000-0000-0000-000000000004  read+write — screen settings JSON
//   CMD:       4fa0b6a0-0000-0000-0000-000000000005  write — commands (start_wifi_ap, reboot)
//
// Telemetry JSON keys (compact — mobile app maps these to signal names):
//   r=RPM, tps=throttle%, map=MAP kPa, bst=boost bar, iat=intake temp,
//   ct=coolant temp, ot=oil temp, op=oil pressure, fp=fuel pressure,
//   lam=lambda, s=speed kph, g=gear, bat=battery V
//   Only valid (non-timed-out) signals are included.

#include "app_config.h"
#include <stdbool.h>
#include <stdint.h>

#if APP_BLE_ENABLED

namespace BleServer {

/**
 * Must be called BEFORE display/LVGL init — reads the BLE-enabled NVS flag
 * directly and calls NimBLEDevice::init() while the heap still has a large
 * contiguous block. After display init, only ~16 KB is contiguous and NimBLE
 * cannot allocate its required ~50 KB. Does nothing if BLE is disabled.
 */
void earlyInit();

/**
 * Called from taskBLE after boot. Completes GATT server + advertising setup
 * using the already-initialized NimBLE stack (no-op if earlyInit() already
 * ran the device init). Reads SettingsPage to honour the persisted toggle.
 */
void init();

/**
 * Start the NimBLE stack at runtime (e.g. after a settings toggle On).
 * No-op if BLE is already running. Same heap guard as init().
 */
void start();

/**
 * Stop BLE advertising, disconnect any active GATT client, and deinit the
 * NimBLE stack to recover ~10 KB DRAM. No-op if BLE is not currently running.
 */
void stop();

/** Returns true when the NimBLE stack is currently initialized and advertising. */
bool isEnabled();

/**
 * Queue a deferred enable/disable request from the settings page (UI task).
 * Consumed by the BLE task via takePendingEnabled().
 */
void setPendingEnabled(bool enabled);

/**
 * Atomically consume the pending enable/disable request.
 * Returns 1 (enable), 0 (disable), or -1 (no pending request).
 * Call from the BLE task.
 */
int8_t takePendingEnabled();

/**
 * Called periodically from the BLE task (~10Hz).
 * Sends telemetry notification if a client is subscribed.
 */
void tick();

/** Returns true if a mobile client is currently connected. */
bool isConnected();

/**
 * Re-compute and push a STATUS characteristic notification.
 * Safe to call from any task — does not require the LVGL mutex.
 */
void pushStatusNotify();

/**
 * Atomically consume the pending day/night toggle request.
 * Returns true (and clears the flag) if a toggle was requested.
 * Call from the UI task, inside the LVGL mutex.
 */
bool takePendingDayNightToggle();

/**
 * Atomically consume the pending explicit day/night set request.
 * Returns 1 (day), 0 (night) or -1 (no pending request) and clears the flag.
 * Call from the UI task, inside the LVGL mutex. Prefer this over the
 * toggle path when both are pending — explicit intent wins.
 */
int8_t takePendingDayNightSet();

/**
 * Atomically consume the pending calibration request.
 * Returns true (and clears the flag) if calibration was requested.
 * Call from the UI task WITHOUT the LVGL mutex — calibrate() is blocking.
 */
bool takePendingCalibration();

/**
 * Atomically consume the pending calibration-reset request.
 * Returns true (and clears the flag) if a reset was requested.
 * Call from the UI task. The reset only touches NVS so no mutex is required.
 */
bool takePendingCalibrationReset();

} // namespace BleServer

#endif // APP_BLE_ENABLED
