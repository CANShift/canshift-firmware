#pragma once
// burn_overlay.h — Full-screen "Saving config…" overlay shown on the LCD
// while the firmware processes a CMD_PUT_CONFIG payload.
//
// Mirrors the studio's BurnProgressModal (#172) so the user gets feedback on
// both screens during the write+reboot cycle. Created on lv_layer_top so it
// floats over whatever page is currently active. The reboot at the end of
// CMD_PUT_CONFIG wipes the screen, so an explicit hide() is rarely needed —
// it's exposed for future flows (hot-reload) that might keep the firmware
// running.
//
// On a write failure the overlay flips to an error state via showError() so
// the LCD doesn't just snap back to the dashboard while studio shows an
// error toast (issue #189). The error state tears itself down after a fixed
// hold via an lv_timer one-shot — callers don't need to schedule teardown.
//
// Must be called while holding g_lvglMutex.

#include <lvgl.h>

namespace BurnOverlay {

/** Reason codes for showError(). Strings live in burn_overlay.cpp. */
enum class ErrorReason {
    WriteFailed,
    ReloadFailed,
};

/** Build and show the overlay. Safe to call repeatedly — re-creates on each call. */
void show();

/** Tear down the overlay. No-op when not currently shown. */
void hide();

/**
 * Replace the spinner state with an error message and schedule auto-teardown
 * after BURN_OVERLAY_ERROR_HOLD_MS. Safe to call without a prior show().
 */
void showError(ErrorReason reason);

} // namespace BurnOverlay
