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

### Calibrate touch

Runs an interactive 3-point calibration. Settings closes itself first so the
crosshairs are unobstructed, then re-opens at the end.

### Reset touch calibration

Wipes the saved offsets. Next boot uses board defaults and re-runs first-boot
calibration. The current session keeps cached offsets so you can still
navigate to reboot.

### Workshop mode

Turns the dash into its own configurator ([design](../architecture/workshop-mode.md)).
The button sits at the bottom of the panel and only appears when both hold:

| Condition | Why |
| --- | --- |
| The firmware is built with `APP_WORKSHOP_ENABLED` (only `waveshare_s3_28`) and runs the 16 MB layout (a filesystem of 4 MB or more) | The tuner ships in flash; the 4 MB boards keep USB configuration and carry none of this code |
| The car is stationary: `speed_kph` reads 0, or no speed signal is mapped, or the speed signal has gone stale | Workshop mode switches the dash off |

Whether it is offered is decided each time the panel opens, and checked again on
the tap, so a car that starts moving with the panel open gets the button hidden
instead of the dash.

Entering:

1. The dash loads its network credentials from NVS (namespace `workshop`),
   generating them on the first entry: network `CANShift-XXXX` and a
   12-character WPA2 password drawn from an alphabet without look-alike
   characters (no `0`/`o`, `1`/`l`/`i`). The same pair is reused on every later
   entry. If they cannot be stored, the dash stays on the dashboard and reports
   the error.
2. The screen switches to the workshop card: *Workshop mode — dash disabled*,
   the network name, the password, a QR code that joins the network in one
   scan, and a **Leave** button. Nothing else on the card reacts to touch.
3. The dashboard pages are released and the top bar, banners and Settings
   panel are hidden.
4. Bluetooth stops and the controller's memory is released, so the mobile app
   disconnects.

CAN decoding and USB keep running. A config pushed over USB in workshop mode is
stored and takes effect when you leave.

**Leave** reboots the dash. Releasing the Bluetooth controller cannot be undone
without a restart, and a reboot brings every subsystem back to a known state.

The access point itself is not up yet: the card shows the credentials it will
use.

### Save / Reset buttons

**Save** writes all current values to NVS and closes the panel. **Reset**
restores brightness to its compile-time default — does not touch
touch calibration.

## What's not in Settings

Dashboard widgets, pages, signals, themes — those live in `dashboard.json` and
are edited from the tuner.
