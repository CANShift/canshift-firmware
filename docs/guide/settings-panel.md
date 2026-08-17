# Settings panel

The Settings panel slides down from the top of the screen and exposes
device-level controls that don't belong in the dashboard config (because they
are per-device, not per-tune).

If a setting silently doesn't apply, the tuner's **Logs** route surfaces what
the firmware actually did with the request — handy to confirm the NVS write
landed or to spot a `Reboot to apply` hint.

## Open / close

| Gesture                            | Action         |
| ---------------------------------- | -------------- |
| Swipe down from the top bar        | Open Settings  |
| Swipe up while open                | Close Settings |
| Tap the top bar with Settings open | Close Settings |

There's a short suppression window (~300 ms) after a swipe-down opens Settings
so the same touch doesn't immediately close it (a tap on the top bar near the
end of a swipe was ambiguous before).

## What lives in Settings

### Brightness slider

0–100 %. Persisted to NVS under namespace `screen_cfg`. Applied to the TFT
backlight PWM in real time as you drag.

### BLE toggle

ON / OFF. Persisted to NVS. Changes apply at next boot — toggling at runtime
shows a "Reboot to apply" hint. Turn off if you don't pair with the mobile app
and want to free the ~50 KB DRAM NimBLE reserves.

### Calibrate touch

Runs an interactive 3-point calibration. Settings closes itself first so the
crosshairs are unobstructed, then re-opens at the end.

### Reset touch calibration

Wipes the saved offsets. Next boot uses board defaults and re-runs first-boot
calibration. The current session keeps cached offsets so you can still
navigate to reboot.

### Save / Reset buttons

**Save** writes all current values to NVS and closes the panel. **Reset**
restores brightness + BLE to their compile-time defaults — does not touch
touch calibration.

## What's not in Settings

Dashboard widgets, pages, signals, themes — those live in `dashboard.json` and
are edited from the tuner.
