#pragma once
// settings_page.h — On-device LVGL screen settings page
//
// A full-canvas overlay (below the top bar) that exposes:
//   - Brightness (slider, live preview via DisplayDriver::setBacklight)
//   - Sleep timeout (segmented buttons: Off / 30s / 1m / 5m)
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

/** Returns the configured sleep timeout in seconds (0 = disabled). */
uint32_t getSleepTimeoutS();

/** Returns the current brightness percentage (10–100). */
uint8_t getBrightness();

// ---------------------------------------------------------------------------
// Drag-to-reveal API (#47) — finger-driven panel pull-down
// ---------------------------------------------------------------------------
// State machine (called from PageManager's drag tracker):
//   beginDrag()      → panel becomes visible, anchored fully off-screen up
//   updateDrag(off)  → panel y translated by clamped offset
//   endDrag(true)    → animate to settled-open
//   endDrag(false)   → animate hidden (and clears s_open)

/** Total panel height in pixels (from init()). */
int16_t getPanelHeight();

/** Resting Y of the panel when open (the yOffset argument from init()). */
int16_t getPanelTopY();

/** Begin a drag-to-open gesture. No-op when already open or already dragging. */
void beginDrag();

/** Update panel position during drag. Clamped internally. */
void updateDrag(int16_t dragOffsetPx);

/** End drag with snap decision: open=true → settled-open; open=false → hidden. */
void endDrag(bool open);

/** True while a drag-to-reveal is in progress (used to suppress swipe gestures). */
bool isDragging();

/**
 * Called each UI tick from PageManager::updateWidgets().
 * Dims backlight after inactivity period; restores on touch.
 * No-op when sleep timeout is 0.
 */
void tickSleep();

/**
 * Apply settings pushed from the desktop Studio over USB.
 * Must be called while holding g_lvglMutex.
 * Applies backlight immediately and persists all values to NVS.
 */
void applyFromUsb(uint8_t brightness, uint32_t sleepTimeoutS);

} // namespace SettingsPage
