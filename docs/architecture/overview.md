# Overview

The firmware is one Arduino-ESP32 binary that wakes up a handful of FreeRTOS tasks, each owning a clearly-defined slice of the runtime. Nothing in here is exotic — the load-bearing decisions are heap budgets, mutex ownership, and a handful of sequencing rules. Get those right and the rest is widgets.

## The runtime, in one picture

```
                      ┌────────────────────────────────────────┐
                      │              taskUI  (core 1)          │
                      │  lv_task_handler · widget tick · gesture│
                      │  owns g_lvglMutex                      │
                      └──┬──────────────┬────────────────┬─────┘
                         │              │                │
                  reads  │       reads  │       reads    │ tick / draw
                         ▼              ▼                ▼
                ┌─────────────┐ ┌──────────────┐ ┌──────────────┐
                │ SignalStore │ │  ErrorStore  │ │  PageManager │
                │  portMUX    │ │  portMUX     │ │  LVGL trees  │
                └──────▲──────┘ └──────▲───────┘ └──────────────┘
                       │ writes        │ writes
       ┌───────────────┴────────────┐  │
       │                            │  │
┌──────┴─────────┐         ┌────────┴──┴────────┐         ┌──────────────────┐
│ taskCAN core 0 │         │   taskUSB  core 1   │         │   taskBLE core 1 │
│ TWAI → decode  │         │ JSON lines · 5 Hz   │         │ NimBLE GATT      │
│ ECU profiles   │         │ PUT_CONFIG · scan   │         │ telemetry mirror │
└────────┬───────┘         └─────────┬───────────┘         └────────┬─────────┘
         │                           │                              │
         ▼                           ▼                              ▼
     CAN bus                      Tuner (USB)                   Mobile app
```

CAN decoding owns **core 0**, at the highest priority, so a busy bus is never starved by the host links. The UI, USB and BLE tasks share **core 1** in priority order. Cross-task state lives in the two stores (`SignalStore`, `ErrorStore`) and is guarded by `portMUX_TYPE` — RMW under a critical section, no LOG / no ALLOC / no LOCK inside it.

## Tasks

| Task    | Core | Priority | Stack  | Period                                                | Entry                                            |
| ------- | ---- | -------- | ------ | ----------------------------------------------------- | ------------------------------------------------ |
| `can`   | 0    | 15       | 4096 B | receive loop, yields `kCanTaskYieldTicks` (1 tick)    | `taskCAN` — `src/main.cpp`                       |
| `ui`    | 1    | 10       | 8192 B | `kLvglHandlerPeriodMs` (10 ms), woken early by notify | `taskUI` — `src/main.cpp`                        |
| `usb`   | 1    | 8        | 4096 B | `kUsbTaskTickIntervalMs` (20 ms)                      | `taskUSBComm` — `src/main.cpp`                   |
| `input` | 0    | 7        | 2048 B | 1 ms, only when buttons are bound                     | `taskInput` — `src/runtime/input_buttons.cpp`    |
| `ble`   | 1    | 6        | 5120 B | `kBleTeleIntervalMs` (100 ms)                         | `taskBLE` — `src/main.cpp`, if `APP_BLE_ENABLED` |

The `TASK_PRIO_*`, `TASK_STACK_*` and `TASK_CORE_*` constants in [`include/app_config.h`](../../include/app_config.h) are the source; this table only reads them. The `ui`, `can`, `usb` and `ble` stacks are allocated statically before boot; the `input` task is spawned only when [`input_bindings.json`](../reference/physical-buttons.md) binds at least one pin.

LVGL reads its clock instead of counting ticks: `LV_TICK_CUSTOM` points `lv_tick_get()` at `canshift_lv_tick_ms()` (`esp_timer_get_time() / 1000`, `src/runtime/lvgl_tick.cpp`), so animations keep wall-clock time when a frame overruns, and no 200 Hz timer wakes core 0 to advance a counter.

## Where to look next

1. **[Boot sequence](boot-sequence.md)** — heap reservation order, the contiguous-block budget, why `lv_init` goes after BLE and before the USB receive buffer.
2. **[LVGL ownership](lvgl-ownership.md)** — who may call `lv_*`, who must take `g_lvglMutex`, and where the deadlocks live.
3. **[Page lifecycle](page-lifecycle.md)** — build, lazy build, release. Why a fresh page hesitates and how the swipe stays smooth.
4. **[USB transport](usb-transport.md)** — the command table, the PUT_CONFIG burn flow, firmware OTA.
5. **[BLE transport](ble-transport.md)** — NimBLE topology, GATT layout, and the stop-race snapshot pattern that keeps disconnect deterministic.
6. **[Cruise template](cruise-template.md)** — the L-shape buttons, the LVGL convex-polygon workaround, and the per-corner Bezier sampling.

Cross-cutting: [ErrorStore](error-store.md), the ring buffer behind the error bar, and [SignalStore](signal-store.md), the runtime signal table `taskCAN` writes and the rest read. Both follow the same critical-section invariant. [Layout scale](layout-scale.md) covers how one layout fits every panel size.

## Three rules that explain most of the source

| Invariant               | Rule                                                                                                                                        |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Heap order              | Anything contiguous > 16 KB must be reserved **before** `lv_init`. The post-init largest free block drops to ~13–18 KB on a no-PSRAM WROOM. |
| LVGL ownership          | Any `lv_*` call from outside `taskUI` must hold `g_lvglMutex`. `taskUI` never needs to take it — it owns the lock by being the only writer. |
| Store critical sections | SignalStore and ErrorStore mutate under `portMUX_TYPE`. **No LOG, no ALLOC, no LOCK** inside `portENTER_CRITICAL`.                          |

## Where the code lives

| Path           | Holds                                                                                                        |
| -------------- | ------------------------------------------------------------------------------------------------------------ |
| `src/main.cpp` | `setup()`, task creation, the task loops                                                                     |
| `src/boot/`    | Boot phases, default font provisioning, the LVGL pool allocator                                              |
| `src/hal/`     | Hardware: `display/`, `touch/`, `storage/` (SPIFFS, NVS), `memory/` (PSRAM), `usb/`, `ble/`                  |
| `src/can/`     | TWAI receive, frame parsing, the signal map, OBD-II                                                          |
| `src/config/`  | JSON config loading and validation, board profile resolution, first-boot defaults                            |
| `src/runtime/` | Signal store, alert engine, action dispatch, physical buttons, OTA receiver, the shared command table        |
| `src/ui/`      | Pages, overlays, settings panel, widgets under `widgets/`                                                    |
| `src/diag/`    | Logger, ErrorStore, perf counters, LVGL asserts                                                              |
| `src/util/`    | Host-portable helpers                                                                                        |
| `include/`     | `app_config.h`, the board profiles under `boards/`, the LovyanGFX factory, `lv_conf.h`, the Rust FFI headers |
| `rust/`        | Pure-logic crates linked into every build, each with a hand-written `include/<name>_rs.h`                    |
| `data/`        | The SPIFFS image: default config, fonts, icons                                                               |
| `test/`        | Unity suites for `pio test -e native`, with host shims under `test/native/shim/`                             |
| `sim/`         | The SDL simulator, `pio run -e sim` ([`sim/README.md`](../../sim/README.md))                                 |
| `scripts/`     | Build hooks, release signing and manifest, CI parity checks, font and icon generators                        |

## Reference shortcuts

- [Pinout](../reference/pinout.md) — GPIO contract for the reference build.
- [Build flags](../reference/build-flags.md) — compile-time switches (log levels, BLE on/off, Rust crates).
- [Signals](../reference/signals.md) — every signal the firmware knows about, with units and source.
- [Flash layout](../reference/flash-layout.md) — partition tables and what lives in SPIFFS.
- [Dev setup](../contributing/dev-setup.md) — sibling clones, per-repo build and CI.
