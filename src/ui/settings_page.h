#pragma once
// settings_page.h — On-device LVGL screen settings page
//
// A full-canvas overlay (below the top bar) that exposes:
//   - Brightness (slider, live preview via DisplayDriver::setBacklight)
//   - Contrast    (slider, stored — hardware support TBD)
//   - Sleep timeout (segmented buttons: Off / 30s / 1m / 5m)
//   - Screen rotation (segmented buttons: 0° / 90° / 180° / 270°)
//
// Navigation:
//   - Opened by tapping the gear icon in the top bar
//   - Closed by tapping the same icon (shows X while open)
//   - Save button persists to NVS (Preferences namespace "screen_cfg")
//   - Reset button restores firmware defaults
//
// USB push path:
//   UsbComm calls applyFromUsb() with the LVGL mutex already held.
//   Values are applied immediately (backlight) and stored to NVS.

#include <stdint.h>

namespace SettingsPage {

/**
     * Create all LVGL objects for the settings overlay.
     * Must be called from TopBar::init(), after LVGL is ready.
     * @param yOffset  Top y-coordinate (= top bar height in pixels)
     * @param height   Available height (= 240 - yOffset)
     */
void init(int16_t yOffset, int16_t height);

/** Show the settings overlay. */
void open();

/** Hide the settings overlay. */
void close();

/** Toggle open/close state. Returns true if now open. */
bool toggle();

/** Current visibility state. */
bool isOpen();

/**
     * Apply settings pushed from the desktop Studio over USB.
     * Must be called while holding g_lvglMutex.
     * Applies backlight immediately, persists all values to NVS.
     *
     * @param brightness    0–100 %
     * @param contrastPct   0–100 %
     * @param sleepTimeoutS seconds (0 = never)
     * @param rotation      0, 90, 180, or 270
     */
void applyFromUsb(uint8_t brightness, uint8_t contrastPct, uint32_t sleepTimeoutS,
                  uint16_t rotation);

} // namespace SettingsPage
