#pragma once
// pending_actions.h — Shared pending-action atomics drained by the UI task.
//
// Every command channel (USB serial, BLE GATT, future MQTT/BT-classic) writes
// to the same flags here instead of carrying its own copy. The UI task in
// main.cpp drains them once per tick, regardless of which channel queued the
// request. Centralising the state means:
//   - Adding a third channel is a one-line change.
//   - main.cpp no longer has to call `takePending*()` once per channel.
//   - Behaviour stays identical: explicit-set wins over toggle when both are
//     pending in the same tick (the `take*` helpers below preserve the
//     drain-order semantics the previous per-channel code relied on).
//
// All flags use `std::memory_order_relaxed` — these are user-driven commands
// arriving at human latency, and the UI task drains them at 100 Hz. No memory
// ordering subtleties matter.

#include <atomic>
#include <stdint.h>

namespace PendingActions {

// Day/night toggle (CMD_TOGGLE_DAY_NIGHT / BLE "toggle_day_night").
inline std::atomic<bool> dayNightToggle{false};

// Explicit day/night set: -1 = none, 0 = night, 1 = day. CMD_SET_DAY_NIGHT
// and BLE "set_day_night" land here. Wins over a concurrent toggle in the
// same tick — the explicit command carries the user's literal intent.
inline std::atomic<int8_t> dayNightSet{-1};

// Touch calibration (CMD_CALIBRATE_TOUCH / BLE "calibrate"). Runs on the
// UI task WITHOUT the LVGL mutex — calibrate() draws directly via TFT_eSPI.
inline std::atomic<bool> touchCalibrate{false};

// Touch calibration reset (CMD_RESET_TOUCH_CAL / BLE "calibrate_reset").
// Synchronous NVS write — no LVGL mutex required.
inline std::atomic<bool> touchCalibrationReset{false};

// BLE pairing passkey request (NimBLE ServerCallbacks::onPassKeyRequest).
// The BLE task encodes "show this 6-digit code" as a non-zero value (range
// 0..999999); 0 means "no pending show". The UI task drains the value once
// per tick and pushes it to PasskeyOverlay::show() inside the LVGL-mutex
// window so the BLE task never touches LVGL directly (issue #873).
inline std::atomic<uint32_t> blePasskeyShow{0};

// BLE pairing finished (NimBLE onAuthenticationComplete) or peer disconnected
// — request the overlay teardown. Independent of `blePasskeyShow` so a
// quick disconnect-then-reconnect cycle in the same tick still hides the
// previous overlay before a fresh passkey replaces it.
inline std::atomic<bool> blePasskeyHide{false};

inline bool takeDayNightToggle() {
    return dayNightToggle.exchange(false, std::memory_order_relaxed);
}

inline int8_t takeDayNightSet() {
    return dayNightSet.exchange(-1, std::memory_order_relaxed);
}

inline bool takeTouchCalibrate() {
    return touchCalibrate.exchange(false, std::memory_order_relaxed);
}

inline bool takeTouchCalibrationReset() {
    return touchCalibrationReset.exchange(false, std::memory_order_relaxed);
}

// Returns the queued passkey (1..999999) or 0 when nothing is pending.
inline uint32_t takeBlePasskeyShow() {
    return blePasskeyShow.exchange(0, std::memory_order_relaxed);
}

inline bool takeBlePasskeyHide() {
    return blePasskeyHide.exchange(false, std::memory_order_relaxed);
}

} // namespace PendingActions
