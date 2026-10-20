# Board drivers

A board profile names one LCD controller and one touch controller. This page lists
what the firmware can actually drive, and what happens when a profile names
something it cannot.

The profile reaches the firmware two ways: baked in at compile time from
`include/boards/<board>.h`, or applied at run time from a `CANSHIFT_BOARD` blob.
Both go through the same factory, so this matrix applies either way.

## LCD controllers

| `LcdDriver` | LovyanGFX panel | Status                                                      |
| ----------- | --------------- | ----------------------------------------------------------- |
| `ILI9341`   | `Panel_ILI9341` | Built. The reference panel on every 320×240 board.          |
| `ST7789`    | `Panel_ST7789`  | Built. Waveshare 2.8 (ST7789T3) and the generic S3 profile. |
| `ILI9488`   | `Panel_ILI9488` | Built, untested on hardware.                                |
| `GC9A01`    | `Panel_GC9A01`  | Built, untested on hardware.                                |

All four are SPI panels. RGB-parallel panels cannot be described by `LcdProfile`
at all — it carries SPI pins and one write frequency, with no fields for the data
bus, sync lines or porch timings. That tier is tracked on #51.

## Touch controllers

| `TouchDriver` | LovyanGFX driver                 | Bus                        | Address | Status                                                                         |
| ------------- | -------------------------------- | -------------------------- | ------- | ------------------------------------------------------------------------------ |
| `None`        | —                                | —                          | —       | A profile may legitimately declare no touch.                                   |
| `XPT2046`     | `Touch_XPT2046`                  | SPI, shared with the panel | —       | Built. Resistive, needs calibration.                                           |
| `GT911`       | `Touch_GT911`                    | I2C                        | `0x5D`  | Built.                                                                         |
| `CST816S`     | `Touch_CST816S`                  | I2C                        | `0x15`  | Built.                                                                         |
| `CST3530`     | `canshift::touch::Touch_CST3530` | I2C                        | `0x58`  | Built. In-tree driver — 4-byte commands, verified on Waveshare hardware (#81). |
| `FT6336`      | `Touch_FT5x06`                   | I2C                        | `0x38`  | Built, untested on hardware. FT6336 is FT5x06 protocol-compatible.             |

## IMUs

| `ImuDriver` | Driver                    | Bus        | Address | Status                                                           |
| ----------- | ------------------------- | ---------- | ------- | ---------------------------------------------------------------- |
| `None`      | —                         | —          | —       | The default; no task, no bus, the g-meter shows `NO IMU`.        |
| `QMI8658`   | `src/hal/imu/qmi8658.cpp` | I2C port 1 | `0x6B`  | Built, accelerometer only, ±4 g at 125 Hz. Untested on hardware. |

See [IMU and the g-meter](../architecture/imu.md) for the axis map and the bench checks.

## Touch calibration

Only a controller whose profile sets `touch.needs_calibration` is calibrated; today
that is the resistive `XPT2046`. Capacitive controllers report screen coordinates
and skip every step below.

- **First boot.** With no calibration in NVS, `TouchDriver::init()` runs
  LovyanGFX's `calibrateTouch()`: the user taps a crosshair in each corner.
- **Storage.** The result is 8 × `uint16_t` in NVS, namespace `touch`, key `cal`,
  loaded on every later boot. A failed write logs and pushes an `nvs_write`
  error rather than reporting success.
- **Again, on demand.** The Settings panel's _Calibrate touch_ row (shown only on
  boards that need it), `kCmdCalibrateTouch` (`0x08`) over USB, or
  `start_calibration` over BLE. `kCmdResetTouchCal` (`0x0A`) / `reset_calibration`
  clears the NVS entry, so the next boot calibrates afresh.

`calibrateTouch()` draws through LovyanGFX, not LVGL, and blocks until the last
tap. The UI task runs it from its pending-action queue without holding
`g_lvglMutex`, and unsubscribes from the task watchdog for the duration.

## When a profile names an unbuilt driver

It used to fall through to `Panel_ILI9341` silently, so a profile asking for
`GC9A01` got an ILI9341 init sequence and undefined output. It now reports:

| Condition              | Log                                                                  | `ErrorStore` code |
| ---------------------- | -------------------------------------------------------------------- | ----------------- |
| LCD driver not built   | `board profile asks for LCD <name>, which this build cannot drive`   | `LCD_DRV`         |
| Touch driver not built | `board profile asks for touch <name>, which this build cannot drive` | `TOUCH_DRV`       |

The panel still falls back to ILI9341 after reporting — a dark screen would leave
no way to surface the error at all — but the fallback is now loud, and the error
reaches the error bar. Touch is simply absent; nothing is substituted.

`configure()` is `[[nodiscard]]` and returns which halves resolved, so the failure
cannot be dropped at the call site.

## For the tuner

When composing a board profile blob, offer only the drivers in these two tables.
A blob naming anything else is accepted by the parser — the wire schema validates
shape, not driver support — and fails at display init, on the device, where the
user cannot see why.

The enums are the contract; their numeric values are the wire encoding and must not
be reordered. Adding a driver means adding it here, in `board_profile.h`, and in
`@canshift/core`'s board profile schema at the same time.
