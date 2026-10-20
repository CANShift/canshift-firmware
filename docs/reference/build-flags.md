# Build flags

Source: [`platformio.ini`](../../platformio.ini)

Compile-time behaviour is steered by macros set in `platformio.ini` build_flags.
This page catalogues the ones contributors hit most often. Defaults live in
[`include/app_config.h`](../../include/app_config.h) — each is wrapped in
`#ifndef`, so a `-D` on the command line or in an env wins.

## Board selection

The flag picks the board the image falls back to when NVS holds no profile — not
the only board it can run. [`include/board.h`](../../include/board.h) reads it and
pulls in the matching profile from [`include/boards/`](../../include/boards/); the
whole chip family's catalog is compiled in either way, and a provisioned board wins
at boot. See [Board profile](../architecture/board-profile.md).

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

No board env sets `BOARD_HAS_PSRAM`. The framework's own sdkconfig initialises
PSRAM whenever the module carries it, `src/hal/memory/psram.cpp` detects it at
run time, and the LVGL pool is sized from what is actually free (below). One
image therefore boots both WROOM and WROVER modules. Only `env:sim` defines the
flag, to give the simulator's LVGL pool the size a PSRAM board gets (#311).

`-DCONFIG_*` flags cannot change the prebuilt sdkconfig: `sdkconfig.h` is read
after the command line and wins. Use the macro the code actually reads, for
example `ARDUINO_LOOP_STACK_SIZE` for the loop task's stack (#313). The
`CONFIG_BT_NIMBLE_*` sizes in `platformio.ini` are the exception: NimBLE-Arduino
reads them itself with `#ifndef`.

## Build profile

| Flag                     | Default                     | Effect                                                                                                                                      |
| ------------------------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `APP_DEBUG_BUILD`        | `0`                         | Extra logging and assertions. Set by `[env:debug]`.                                                                                         |
| `APP_SECURE_BOOT_BUILD`  | `0`                         | Set by the CrowPanel `[env:secure]`, read by no code. It does not make a build secure: see [Secure boot setup](secure-boot-setup.md).       |
| `APP_LOG_LEVEL`          | `1` (release) / `4` (debug) | `0=none 1=error 2=warn 3=info 4=debug 5=verbose`. Release stays at `error` — info leaks signal lifecycle (rpm, throttle, lambda) over UART. |
| `APP_VERBOSE_DEBUG_LOGS` | `0` (release) / `1` (debug) | Gates `LOG_VDEBUG` call sites — they collapse to no-ops at preprocess time when disabled.                                                   |
| `CORE_DEBUG_LEVEL`       | `1`                         | Framework logging (arduino-esp32, LovyanGFX, NimBLE). Held at errors-only to drop their format strings; app logs are unaffected.            |

## Transport

| Flag              | Default | Effect                                                                                                  |
| ----------------- | ------- | ------------------------------------------------------------------------------------------------------- |
| `APP_BLE_ENABLED` | `1`     | `0` excludes NimBLE entirely (~30 KB DRAM saved). This is the only switch — there is no runtime toggle. |

USB CDC is not behind a flag — it is the primary transport and always compiled
in. See [Transports](../architecture/transports.md).

## Audio

| Flag                      | Default | Effect                                                                                                                                                     |
| ------------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `APP_AUDIO_ENABLED`       | `0`     | Compiles the I2S tone output and the sound task. Set only by `waveshare_s3_28`. At run time the task still starts only when the board profile has `audio`. |
| `USE_RUST_TONE_SEQUENCER` | unset   | Links `rust/tone-sequencer`. It goes with `APP_AUDIO_ENABLED`; no other env references the crate.                                                          |

A board without audio output pays nothing: the CrowPanel and generic envs compile
neither the C++ nor the crate. The sounds themselves are specified in §10b of the
[design system](../design/DASH_DESIGN_SYSTEM.md).

## Rust modules

Ten crates under [`rust/`](../../rust/) are compiled into every board build.
They are **not opt-in**: the base env sets all ten flags and the C++ callers
invoke the FFI unconditionally, so an env that rebuilds `build_flags` from
`base_flags` alone will fail to link. Envs that extend a board env must inherit
`${env:crowpanel_28.build_flags}` rather than reconstruct it.

| Flag                     | Crate                | What lives in Rust                             |
| ------------------------ | -------------------- | ---------------------------------------------- |
| `USE_RUST_SIGNAL_MAP`    | `rust/signal-map`    | Name → `SignalId` lookup                       |
| `USE_RUST_CAN_PARSER`    | `rust/can-parser`    | `decodeBytes` byte-range → f32                 |
| `USE_RUST_USB_ENVELOPE`  | `rust/usb-envelope`  | `PUT_CONFIG` envelope brace walk               |
| `USE_RUST_CONFIG_LOADER` | `rust/config-loader` | `parseMajorVersion`                            |
| `USE_RUST_FORMAT_FLOAT`  | `rust/format-float`  | Newlib-free `%f` / `%g` formatters             |
| `USE_RUST_ERROR_STORE`   | `rust/error-store`   | Ring-buffer push / getAll / dismissAt          |
| `USE_RUST_ALERT_ENGINE`  | `rust/alert-engine`  | Per-signal threshold evaluators                |
| `USE_RUST_TIMER_CORE`    | `rust/timer-core`    | Lap and stopwatch state                        |
| `USE_RUST_LAYOUT_GRID`   | `rust/layout-grid`   | Grid cell → pixel rect resolution              |
| `USE_RUST_CONTROL_STATE` | `rust/control-state` | Cruise and button control state machines       |
| `USE_RUST_UNIT_CONVERT`  | `rust/unit-convert`  | Metric ↔ imperial pairs for the rendered value |

Each flag turns on the matching feature of [`rust/canshift-ffi`](../../rust/canshift-ffi/),
the one staticlib the image links. The member crates are rlibs inside it, so the
release profile's LTO runs across all of them, there is a single `core` and a
single panic handler, and the link needs no `-z muldefs`. A Rust panic calls C
`abort()`, which leaves a coredump.

## Hardware

| Flag                        | Default | Effect                                                                                                                  |
| --------------------------- | ------: | ----------------------------------------------------------------------------------------------------------------------- |
| `HW_LVGL_DRAW_BUDGET_BYTES` |   25 KB | RAM budget for both LVGL draw buffers combined. Drives the line-count computation. Set in `include/hardware_profile.h`. |
| `TASK_WDT_TIMEOUT_MS`       |    8000 | Watchdog timeout for the UI, CAN and USB tasks. Long enough to survive a page rebuild plus a SPIFFS font load.          |
| `kLvglFsMinHeapBytes`       |     256 | Below this the LVGL FS driver refuses opens, keeping newlib's `__sfp` out of `abort()`.                                 |
| `LVGL_POOL_FORCE_FAIL`      |       0 | Fault injection for the LVGL pool allocator. Makes the first _n_ capability tiers refuse. Never set in a shipped env.   |

### The LVGL pool and its failure path

On hardware LVGL allocates through `LV_MEM_CUSTOM` from one region claimed at
`lv_init()` (`src/boot/lvgl_pool_alloc.cpp`, an ESP-IDF `multi_heap` over that
region), so the pool size is decided at run time:

| Module                         | Pool                                                                                                                 |
| ------------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| PSRAM with a free 512 KB block | 512 KB in PSRAM                                                                                                      |
| no PSRAM (WROOM)               | 80 KB of internal RAM, less if the largest free block minus 16 KB headroom is smaller, halving down to a 32 KB floor |

Only when not even 32 KB of internal RAM is free does it restart. With the
rollback gate armed ([Boot sequence](../architecture/boot-sequence.md#ota-mark-valid-placement)),
an OTA image that dies there rolls back to the previous slot.
`LVGL_POOL_FORCE_FAIL` exercises the ladder on a board that has PSRAM:

| Value | What refuses           | What you should see                                                                |
| ----: | ---------------------- | ---------------------------------------------------------------------------------- |
|   `0` | nothing                | `pool: 524288 B in PSRAM` on a PSRAM board, `pool: N B in internal RAM` on a WROOM |
|   `1` | PSRAM                  | `pool: N B in internal RAM`, the WROOM path on PSRAM hardware                      |
|   `2` | PSRAM and internal RAM | `no pool: … restarting`, then a reboot loop, or a rollback after an OTA            |

```
PLATFORMIO_BUILD_FLAGS="-DLVGL_POOL_FORCE_FAIL=1" pio run -e waveshare_s3_28 -t upload
```

## Tracing

All default to `0` and are bundled by `[env:debug-perf]`.

| Flag                         | Effect                                                                                               |
| ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| `APP_PROFILE_UI`             | 1 Hz UI perf line — mutex wait, widget update, frame total, FPS, misses — plus the LVGL FPS overlay. |
| `APP_LV_TASK_LOG`            | 1 Hz `lv_task: avg/max/n` line. Orthogonal to `APP_PROFILE_UI`.                                      |
| `APP_USB_TICK_TRACE`         | Logs USB-tick interval and body duration above their thresholds.                                     |
| `APP_USB_CAN_SCAN_FAIL_LOUD` | Aborts the firmware when a CAN-scan queue allocation fails, instead of degrading quietly.            |

## Build envs

| Env                                        | What it is                                                                                                                                                                                               |
| ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `crowpanel_28`                             | Production build for the reference hardware. The base every other board env extends.                                                                                                                     |
| `generic_ili9341`, `generic_ili9341_gt911` | Portability legs — pinouts unverified on hardware.                                                                                                                                                       |
| `waveshare_s3_28`                          | ESP32-S3 board, verified on hardware.                                                                                                                                                                    |
| `generic_esp32s3`                          | Chip-family compile target. Every LCD and touch pin in its profile is `-1`, so it drives nothing as flashed — it exists to prove the S3 target builds and to be the base of a runtime-provisioned image. |
| `debug`                                    | `APP_DEBUG_BUILD=1`, framework logging at level 5, slow upload.                                                                                                                                          |
| `debug-perf`                               | Production logging with the tracing bundle compiled in.                                                                                                                                                  |
| `secure`                                   | Secure boot v2 + flash encryption, on the secure partition table.                                                                                                                                        |
| `native`                                   | Host build for `pio test -e native`.                                                                                                                                                                     |
| `sim`                                      | Native SDL simulator — the UI without hardware.                                                                                                                                                          |

Override any flag per env with `build_flags = -DFOO=1`, but extend a board env
rather than `base_flags` so the Rust flags come along.

## Which boards exist, which CI builds, which ones ship

Three lists describe board support. They are not the same list, and
`scripts/check_board_lists.py` gates the first two on every PR.

| List                              | Answers                                             | Today                    |
| --------------------------------- | --------------------------------------------------- | ------------------------ |
| `include/boards/*.h`              | what the firmware can be built for                  | 5                        |
| `.github/boards.json`             | what CI builds, and which of those a release offers | 5, of which 4 releasable |
| `@canshift/core` `BOARD_PROFILES` | what the tuner can offer                            | 4                        |

The invariants the gate enforces:

- one board header per `boards.json` entry, **under the same id** — the header's
  `.board_id`, its filename and the entry's `id` all match;
- a `[env:<id>]` in `platformio.ini` for every entry, so the matrix can build it;
- every chip family with releasable boards has **exactly one** `universal: true`
  entry — the env its published firmware is built from, and itself releasable.

The third list is checked from the other side: core's `firmware-parity` job runs
`board-profile.test.ts` against this repo's `main` and fails unless `BOARD_PROFILES`
equals the releasable subset — nothing offered that cannot be flashed, nothing
published that the tuner cannot name. A board change lands here first, then in core.

`generic_esp32s3` is the one entry with `release: false`. It compiles, so CI covers
it, but offering a profile whose every pin is `-1` would give a user a dark screen.

**For the tuner:** `manifest.json` is the contract. A board appears there only if it
is releasable, and each entry names its `chip`, which is what resolves the binary the
flasher writes and where that binary's bootloader sits — the artifacts themselves are
published per chip family, not per board. Do not infer the board list from
`platformio.ini` or from `include/boards/` — those are build concerns and both are
longer than the shipped list.
