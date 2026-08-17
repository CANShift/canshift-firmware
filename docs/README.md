# Firmware documentation

Everything about the code that runs on the dash. If you have never flashed a
CANShift board before, read [What is CANShift?](guide/overview.md) first, then
follow [First flash](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/first-flash.md)
in the tuner repo — the flasher lives there.

Docs for the other repos: [tuner](https://github.com/CANShift/canshift-tuner/tree/main/docs) ·
[core](https://github.com/CANShift/canshift-core/tree/main/docs) ·
[mobile](https://github.com/CANShift/canshift-mobile/tree/main/docs)

## Using the dash

No C++ needed — this is the on-device UI as a driver sees it.

| Doc                                           | What it covers                                               |
| --------------------------------------------- | ------------------------------------------------------------ |
| [What is CANShift?](guide/overview.md)        | What the dash does, what it talks to, and what you will need |
| [Navigating pages](guide/navigating-pages.md) | Swipe gestures, page reordering, default page on boot        |
| [Settings panel](guide/settings-panel.md)     | Brightness, BLE toggle, touch calibration                    |
| [Cruise control](guide/cruise-control.md)     | Using the cruise page on the dash                            |

## Building one

| Doc                                                   | What it covers                                                       |
| ----------------------------------------------------- | -------------------------------------------------------------------- |
| [Hardware & BOM](reference/hardware-bom.md)           | Every part needed for one dash, with prices and suppliers per region |
| [Pinout](reference/pinout.md)                         | GPIO reference for the ESP32 board, ILI9341 display, XPT2046 touch   |
| [Display bus](reference/pinout-display.md)            | Panel and touch controller on one shared SPI bus                     |
| [CAN header](reference/pinout-can.md)                 | The two TWAI pins and the transceiver they need                      |
| [Power rails](reference/pinout-power.md)              | Where the board takes power, and the rail the firmware modulates     |
| [CAN integration notes](reference/can-integration.md) | TWAI configuration, bus speed, sample frame layouts                  |

## Architecture

How the firmware is put together — read [Overview](architecture/overview.md) first.

| Doc                                                | What it covers                                                              |
| -------------------------------------------------- | --------------------------------------------------------------------------- |
| [Overview](architecture/overview.md)               | What runs in which task, against which heap budget                          |
| [Boot sequence](architecture/boot-sequence.md)     | Heap reservation ordering that makes the boot path work on a no-PSRAM WROOM |
| [SignalStore](architecture/signal-store.md)        | Runtime signal table — taskCAN writes, taskUI reads                         |
| [Page lifecycle](architecture/page-lifecycle.md)   | Build, lazy build, release — the dashboard page state machine               |
| [LVGL ownership](architecture/lvgl-ownership.md)   | Mutex and thread rules for every `lv_*` call                                |
| [Transports](architecture/transports.md)           | The two host links — USB CDC always, BLE GATT when compiled in              |
| [USB transport](architecture/usb-transport.md)     | The serial sink, the command table, the `PUT_CONFIG` burn flow              |
| [BLE transport](architecture/ble-transport.md)     | NimBLE topology, encrypted GATT layout, binary telemetry frame              |
| [ErrorStore](architecture/error-store.md)          | Error ring buffer and its critical-section invariant                        |
| [Cruise template](architecture/cruise-template.md) | L-shape buttons and the LVGL convex-polygon workaround                      |

## Reference

| Doc                                                 | What it covers                                           |
| --------------------------------------------------- | -------------------------------------------------------- |
| [Signal map](reference/signals.md)                  | Known signals — ids, units, and where they come from     |
| [Build flags](reference/build-flags.md)             | PlatformIO `build_flags` that change firmware behaviour  |
| [ECU integration](reference/ecu-integration.md)     | Catalogue format, frame parser, CAN XML import           |
| [Firmware signing](reference/firmware-signing.md)   | Ed25519 signature workflow for shipped binaries          |
| [Secure boot setup](reference/secure-boot-setup.md) | Secure boot v2 and flash encryption on production builds |

The JSON contract shared with the tuner and the mobile app lives in core:
[config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md) ·
[wire protocol versioning](https://github.com/CANShift/canshift-core/blob/main/docs/wire-protocol-versioning.md).

## Contributing

| Doc                                                | What it covers                                                          |
| -------------------------------------------------- | ----------------------------------------------------------------------- |
| [Dev setup](contributing/dev-setup.md)             | Clone the repos as siblings and build them locally                      |
| [Testing](contributing/testing.md)                 | Unity (C++), `cargo test` (Rust), Vitest (TS)                           |
| [Add a widget](contributing/add-widget.md)         | A new LVGL widget end-to-end across core, firmware, tuner               |
| [Add an ECU profile](contributing/add-ecu.md)      | Wire a new ECU's signals into the shared schema and ship it as a preset |
| [Release process](contributing/release-process.md) | How firmware releases and the `@canshift/core` package are cut          |

## Design spec

[`design/DASH_DESIGN_SYSTEM.md`](design/DASH_DESIGN_SYSTEM.md) is the binding
specification for the dash, and [`design/DASH_PAGES.json`](design/DASH_PAGES.json)
holds the six default pages as data. Where the firmware differs from them, the
firmware is wrong. Sample configurations live in [`examples/`](examples/).

## Elsewhere

- [Roadmap](roadmap.md) — planned work across all repos
- [Releases](https://github.com/CANShift/canshift-firmware/releases) — changelog and downloadable images
