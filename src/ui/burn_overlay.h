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
// Must be called while holding g_lvglMutex.

#include <lvgl.h>

namespace BurnOverlay {

/** Build and show the overlay. Safe to call repeatedly — re-creates on each call. */
void show();

/** Tear down the overlay. No-op when not currently shown. */
void hide();

} // namespace BurnOverlay
