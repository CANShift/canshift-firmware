#pragma once
// passkey_overlay.h — On-screen 6-digit BLE pairing passkey, shown for the
// duration of a NimBLE pairing handshake.
//
// NimBLE generates a fresh passkey via `ServerCallbacks::onPassKeyRequest()`
// each time a mobile peer initiates pairing. The user reads the passkey here
// and types it into the canshift-mobile app — this is the "physical
// confirmation" gate that bonds the device to a single trusted phone (#873,
// #878).
//
// Built on lv_layer_top so it floats over whatever page is currently active.
// The BLE callbacks live on the BLE task and cannot touch LVGL directly —
// they hand the passkey off via `PendingActions::blePasskeyShow`, and the UI
// task drains the flag inside its existing LVGL-mutex window before calling
// `show()` / `hide()` here.
//
// All public functions must be called while holding `g_lvglMutex`.

#include <lvgl.h>
#include <stdint.h>

namespace PasskeyOverlay {

/**
 * Show the 6-digit pairing passkey. Safe to call repeatedly — re-creates the
 * overlay on each call so a fresh passkey replaces any previous one.
 *
 * `passkey` is the value to display (0..999999). Larger values are clamped.
 */
void show(uint32_t passkey);

/** Tear down the overlay. No-op when not currently shown. */
void hide();

} // namespace PasskeyOverlay
