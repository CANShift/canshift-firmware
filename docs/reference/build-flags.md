# Build flags

Source: [`platformio.ini`](../../platformio.ini)

The firmware's compile-time behaviour is steered by macros set in
`platformio.ini` build_flags. This page catalogs the flags users + contributors
hit most often. All defaults live in `canshift-firmware/include/app_config.h`.

## Build profile

| Flag                     | Default                     | Effect                                                                                                                                             |
| ------------------------ | --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `APP_DEBUG_BUILD`        | `0`                         | Enables extra logging + assertions. Set by `[env:debug]`.                                                                                          |
| `APP_SECURE_BOOT_BUILD`  | `0`                         | Marks the image as a secure-boot + flash-encryption build. Set by `[env:secure]`. Fuse-burn is host-side via `scripts/secure_boot_first_flash.sh`. |
| `APP_LOG_LEVEL`          | `1` (release) / `4` (debug) | `0=none 1=error 2=warn 3=info 4=debug 5=verbose`. Release stays at `error` — info leaks signal lifecycle (rpm, throttle, lambda) over UART (#899). |
| `APP_VERBOSE_DEBUG_LOGS` | `0` (release) / `1` (debug) | Gates `LOG_VDEBUG` call sites — collapses to no-ops at preprocess time when disabled.                                                              |

## Transport

| Flag                   | Default | Effect                                                                                                       |
| ---------------------- | ------- | ------------------------------------------------------------------------------------------------------------ |
| `APP_BLE_ENABLED`      | `1`     | `0` excludes NimBLE entirely (~30 KB DRAM saved).                                                            |
| `BLE_DEFAULT_ENABLED`  | `1`     | Runtime BLE on/off when NimBLE is compiled in. Mutually exclusive with WiFi AP at boot.                      |
| `APP_WIFI_OTA_ENABLED` | `0`     | Drops WiFi + WebServer + Update libs (~80 KB flash). Re-enable when phase 3 (BLE-driven OTA) needs it (#48). |
| `APP_SPA_SERVE`        | `0`     | Embeds the studio SPA bundle for serving over WiFi. Requires `APP_WIFI_OTA_ENABLED=1`.                       |

## Rust ports (opt-in)

Each Rust crate is opt-in via a `USE_RUST_*=1` build_flag so CI without the
`espup` toolchain keeps passing on the default envs:

| Flag                         | Crate                    | What moves to Rust                                |
| ---------------------------- | ------------------------ | ------------------------------------------------- |
| `USE_RUST_OTA_HMAC`          | `rust/ota-hmac`          | Streaming HMAC-SHA256 trailer verifier (#827)     |
| `USE_RUST_SIGNAL_MAP`        | `rust/signal-map`        | Name → SignalId lookup (#1177 R-4)                |
| `USE_RUST_CAN_PARSER`        | `rust/can-parser`        | `decodeBytes` byte-range → f32 (#1177 R-1)        |
| `USE_RUST_USB_ENVELOPE`      | `rust/usb-envelope`      | PUT_CONFIG envelope brace walk (#1177 R-7)        |
| `USE_RUST_CONFIG_LOADER`     | `rust/config-loader`     | `parseMajorVersion` (#1177 R-10)                  |
| `USE_RUST_FORMAT_FLOAT`      | `rust/format-float`      | Newlib-free `%f`/`%g` formatters (#1177 R-2)      |
| `USE_RUST_ERROR_STORE`       | `rust/error-store`       | Ring-buffer push / getAll / dismissAt (#1177 R-5) |
| `USE_RUST_ALERT_ENGINE`      | `rust/alert-engine`      | Per-signal threshold evaluators (#1177 R-6)       |
| `USE_RUST_SENSOR_COLOR_RAMP` | `rust/sensor-color-ramp` | Ramp sampling + sensor-kind heuristic (#1177 R-3) |

## Hardware

| Flag                        | Default    | Effect                                                                                         |
| --------------------------- | ---------- | ---------------------------------------------------------------------------------------------- |
| `HW_TFT_FAST_SPI`           | `0`        | Switches the TFT SPI clock from 27 → 40 MHz. Opt-in after hardware validation (#95 F6).        |
| `HW_LVGL_DRAW_BUDGET_BYTES` | `~12.8 KB` | RAM budget for both LVGL draw buffers combined. Drives line-count computation.                 |
| `TASK_WDT_TIMEOUT_MS`       | `8000`     | Watchdog timeout for UI/CAN/USB tasks. Long enough to survive page rebuild + SPIFFS font load. |
| `LVGL_FS_MIN_HEAP_BYTES`    | `256`      | Below this, the LVGL FS driver refuses opens to keep newlib `__sfp` out of abort() (#651).     |

## Tracing / instrumentation

| Flag                         | Default | Effect                                                                                                                |
| ---------------------------- | ------- | --------------------------------------------------------------------------------------------------------------------- |
| `APP_PROFILE_UI`             | `0`     | Emits per-frame UI perf line at 1 Hz: mutex wait, widget update, frame total, FPS, misses. Set by `[env:debug-perf]`. |
| `APP_LV_TASK_LOG`            | `0`     | 1 Hz "lv_task: avg/max/n" line. Orthogonal to `APP_PROFILE_UI`.                                                       |
| `APP_USB_TICK_TRACE`         | `0`     | Logs USB-tick interval + body duration above their thresholds. Repro tool for #976.                                   |
| `APP_USB_CAN_SCAN_FAIL_LOUD` | `0`     | Aborts the firmware when CAN-scan queue alloc fails (#976 repro).                                                     |

## Build envs

Common PlatformIO envs and the flag bundles they enable:

```ini
[env:crowpanel_28]          # production — APP_BLE_ENABLED=1, secure off
[env:crowpanel_28_ota]      # OTA-capable variant
[env:debug]                 # APP_DEBUG_BUILD=1 + APP_LOG_LEVEL=4
[env:debug-perf]            # debug + APP_PROFILE_UI=1
[env:secure]                # secure-boot v2 + flash encryption
[env:crowpanel_28_rust]     # USE_RUST_OTA_HMAC=1
[env:crowpanel_28_rust_*]   # one env per Rust port — see scripts/build_rust.py
[env:sim]                   # native host build (no hardware)
[env:native]                # unit-test target (Unity + cargo test)
```

Override any flag per env via `build_flags = -DFOO=1`.
