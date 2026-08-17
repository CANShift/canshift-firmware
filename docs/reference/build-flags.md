# Build flags

Source: [`platformio.ini`](../../platformio.ini)

Compile-time behaviour is steered by macros set in `platformio.ini` build_flags.
This page catalogues the ones contributors hit most often. Defaults live in
[`include/app_config.h`](../../include/app_config.h) — each is wrapped in
`#ifndef`, so a `-D` on the command line or in an env wins.

## Board selection

The board is picked at compile time. [`include/board.h`](../../include/board.h)
reads the flag and pulls in the matching profile from
[`include/boards/`](../../include/boards/).

| Flag                          | Board                     |
| ----------------------------- | ------------------------- |
| `BOARD_CROWPANEL_28`          | Elecrow CrowPanel 2.8"    |
| `BOARD_GENERIC_ILI9341`       | Generic ILI9341 + XPT2046 |
| `BOARD_GENERIC_ILI9341_GT911` | Generic ILI9341 + GT911   |
| `BOARD_GENERIC_ESP32S3`       | Generic ESP32-S3          |
| `BOARD_WAVESHARE_S3_28`       | Waveshare ESP32-S3 2.8"   |

Exactly one is set per env. A board env unflags the base
`-DBOARD_CROWPANEL_28=1` before setting its own — see
[Adding a board](../../README.md#adding-a-board).

`BOARD_HAS_PSRAM` enables the IDF PSRAM init so WROVER variants light up
automatically. On a WROOM chip with no PSRAM the init fails silently and the
runtime detect in `src/hal/memory/psram.cpp` handles it.

## Build profile

| Flag                     | Default                     | Effect                                                                                                                                             |
| ------------------------ | --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `APP_DEBUG_BUILD`        | `0`                         | Extra logging and assertions. Set by `[env:debug]`.                                                                                                |
| `APP_SECURE_BOOT_BUILD`  | `0`                         | Marks the image as a secure-boot + flash-encryption build. Set by `[env:secure]`. Fuse-burn is host-side via `scripts/secure_boot_first_flash.sh`. |
| `APP_LOG_LEVEL`          | `1` (release) / `4` (debug) | `0=none 1=error 2=warn 3=info 4=debug 5=verbose`. Release stays at `error` — info leaks signal lifecycle (rpm, throttle, lambda) over UART.        |
| `APP_VERBOSE_DEBUG_LOGS` | `0` (release) / `1` (debug) | Gates `LOG_VDEBUG` call sites — they collapse to no-ops at preprocess time when disabled.                                                          |
| `CORE_DEBUG_LEVEL`       | `1`                         | Framework logging (arduino-esp32, LovyanGFX, NimBLE). Held at errors-only to drop their format strings; app logs are unaffected.                   |

## Transport

| Flag                  | Default | Effect                                                                                                                      |
| --------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------- |
| `APP_BLE_ENABLED`     | `1`     | `0` excludes NimBLE entirely (~30 KB DRAM saved).                                                                           |
| `BLE_DEFAULT_ENABLED` | `1`     | Whether BLE starts enabled at boot when NimBLE is compiled in. The user's Settings toggle overrides it and persists in NVS. |

USB CDC is not behind a flag — it is the primary transport and always compiled
in. See [Transports](../architecture/transports.md).

## Rust modules

Ten crates under [`rust/`](../../rust/) are compiled into every board build.
They are **not opt-in**: the base env sets all ten flags and the C++ callers
invoke the FFI unconditionally, so an env that rebuilds `build_flags` from
`base_flags` alone will fail to link. Envs that extend a board env must inherit
`${env:crowpanel_28.build_flags}` rather than reconstruct it.

| Flag                     | Crate                | What lives in Rust                       |
| ------------------------ | -------------------- | ---------------------------------------- |
| `USE_RUST_SIGNAL_MAP`    | `rust/signal-map`    | Name → `SignalId` lookup                 |
| `USE_RUST_CAN_PARSER`    | `rust/can-parser`    | `decodeBytes` byte-range → f32           |
| `USE_RUST_USB_ENVELOPE`  | `rust/usb-envelope`  | `PUT_CONFIG` envelope brace walk         |
| `USE_RUST_CONFIG_LOADER` | `rust/config-loader` | `parseMajorVersion`                      |
| `USE_RUST_FORMAT_FLOAT`  | `rust/format-float`  | Newlib-free `%f` / `%g` formatters       |
| `USE_RUST_ERROR_STORE`   | `rust/error-store`   | Ring-buffer push / getAll / dismissAt    |
| `USE_RUST_ALERT_ENGINE`  | `rust/alert-engine`  | Per-signal threshold evaluators          |
| `USE_RUST_TIMER_CORE`    | `rust/timer-core`    | Lap and stopwatch state                  |
| `USE_RUST_LAYOUT_GRID`   | `rust/layout-grid`   | Grid cell → pixel rect resolution        |
| `USE_RUST_CONTROL_STATE` | `rust/control-state` | Cruise and button control state machines |

`-Wl,-z,muldefs` accompanies them: the Rust staticlib and newlib both define a
handful of symbols, and the linker takes the first.

## Hardware

| Flag                        | Default | Effect                                                                                                                  |
| --------------------------- | ------: | ----------------------------------------------------------------------------------------------------------------------- |
| `HW_LVGL_DRAW_BUDGET_BYTES` |   25 KB | RAM budget for both LVGL draw buffers combined. Drives the line-count computation. Set in `include/hardware_profile.h`. |
| `TASK_WDT_TIMEOUT_MS`       |    8000 | Watchdog timeout for the UI, CAN and USB tasks. Long enough to survive a page rebuild plus a SPIFFS font load.          |
| `LVGL_FS_MIN_HEAP_BYTES`    |     256 | Below this the LVGL FS driver refuses opens, keeping newlib's `__sfp` out of `abort()`.                                 |

## Tracing

All default to `0` and are bundled by `[env:debug-perf]`.

| Flag                         | Effect                                                                                               |
| ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| `APP_PROFILE_UI`             | 1 Hz UI perf line — mutex wait, widget update, frame total, FPS, misses — plus the LVGL FPS overlay. |
| `APP_LV_TASK_LOG`            | 1 Hz `lv_task: avg/max/n` line. Orthogonal to `APP_PROFILE_UI`.                                      |
| `APP_USB_TICK_TRACE`         | Logs USB-tick interval and body duration above their thresholds.                                     |
| `APP_USB_CAN_SCAN_FAIL_LOUD` | Aborts the firmware when a CAN-scan queue allocation fails, instead of degrading quietly.            |

## Build envs

| Env                                          | What it is                                                                           |
| -------------------------------------------- | ------------------------------------------------------------------------------------ |
| `crowpanel_28`                               | Production build for the reference hardware. The base every other board env extends. |
| `generic_ili9341`, `generic_ili9341_gt911`   | Portability legs — pinouts unverified on hardware.                                   |
| `waveshare_s3_28`                            | ESP32-S3 board, verified on hardware.                                                |
| `esp32s3`                                    | Chip-family compile target. Every LCD and touch pin in its profile is `-1`, so it drives nothing as flashed — it exists to prove the S3 target builds and to be the base of a runtime-provisioned image. |
| `debug`                                               | `APP_DEBUG_BUILD=1`, framework logging at level 5, slow upload.                      |
| `debug-perf`                                          | Production logging with the tracing bundle compiled in.                              |
| `secure`                                              | Secure boot v2 + flash encryption, on the secure partition table.                    |
| `native`                                              | Host build for `pio test -e native`.                                                 |
| `sim`                                                 | Native SDL simulator — the UI without hardware.                                      |

Override any flag per env with `build_flags = -DFOO=1`, but extend a board env
rather than `base_flags` so the Rust flags come along.

## Which boards CI builds, which ones ship

`.github/boards.json` is the single list behind both matrices. Every entry carries
`id`, `chip`, `display`, `touch` and a `release` flag:

| Consumer                        | Filter                          | Result                                   |
| ------------------------------- | ------------------------------- | ---------------------------------------- |
| `build.yml` — the `firmware` job | every entry                     | all five envs build on every PR          |
| `release.yml` — artifacts        | `select(.release)`              | four boards get merged/firmware/spiffs bins |
| `release.yml` — `manifest.json`  | `select(.release)`              | the board list the tuner flasher reads   |

`esp32s3` is the one entry with `release: false`. It compiles, so CI must cover it,
but publishing a flashable artifact for a profile whose every pin is `-1` would give
a user a dark screen. It becomes releasable once a board profile can be provisioned
at flash time (#247).

**For the tuner:** `manifest.json` is the contract. A board appears there only if it
is releasable, and each entry names its `chip`, which decides which binary the
flasher writes. Do not infer the board list from `platformio.ini` or from
`include/boards/` — those are build concerns and both are longer than the shipped
list.
