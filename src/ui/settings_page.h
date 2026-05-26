#pragma once
// settings_page.h — On-device LVGL screen settings page
//
// A full-canvas overlay (below the top bar) that exposes:
//   - Brightness (slider, live preview via DisplayDriver::setBacklight)
//   - Bluetooth (ON/OFF toggle — persisted in NVS, applied immediately)
//   - Calibrate Touch (runs TFT_eSPI crosshair calibration, stores to NVS)
//
// Navigation:
//   - Opened by swiping down from the top of the screen (via top bar gesture)
//   - Closed by tapping the X icon in the top bar
//   - Save button persists to NVS (Preferences namespace "screen_cfg")
//   - Reset button restores firmware defaults
//
// USB push path:
//   UsbComm calls applyFromUsb() with the LVGL mutex already held.
//   Values are applied immediately (backlight) and stored to NVS.

#include <stdint.h>

namespace SettingsPage {

/** Create all LVGL objects for the settings overlay. Must be called after LVGL is ready. */
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
 * Millis at which open() was last called. Used by the top bar to debounce
 * a tap that immediately follows a gesture-driven open (#50).
 */
uint32_t lastOpenMs();

/**
 * Drag-to-reveal helpers (issue #47).
 *
 * The panel sits at y=getOpenY() when fully visible and y=getClosedY()
 * (which is just above the screen, equal to -getPanelHeight() + topBarHeight)
 * when fully hidden. setPanelY() lets a gesture controller drag the panel
 * between the two without flipping the open/closed flag.
 *
 * snapOpen()/snapClosed() animate from the current y to the target and update
 * the open flag + visibility on completion.
 *
 * All must be called while holding g_lvglMutex.
 */
int16_t getOpenY();
int16_t getClosedY();
int16_t getPanelHeight();
void setPanelY(int16_t y);
void snapOpen();
void snapClosed();

/** True when a drag-to-reveal gesture is currently in progress. */
bool isDragging();
void setDragging(bool dragging);

/** Returns the current brightness percentage (10–100). */
uint8_t getBrightness();

/** Returns true when BLE is enabled (default: true). */
bool getBleEnabled();

/**
 * Returns true when the WiFi AP auto-start preference is on (mirrors NVS
 * value owned by WifiAp). Default false on a fresh device — the AP only
 * comes up after the user opts in via the WIFI AP toggle row (#1077).
 */
bool getWifiApAutoStart();

/**
 * Apply settings pushed from the desktop Studio over USB.
 * Must be called while holding g_lvglMutex.
 * Applies backlight immediately and persists to NVS.
 */
void applyFromUsb(uint8_t brightness);

} // namespace SettingsPage
