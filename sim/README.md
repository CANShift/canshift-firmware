# Native simulator (#136)

Runs the full dash UI in an SDL window on macOS/Linux — pages, widget grammar,
alert takeover, OTA screens — from the bundled default config, with signals
driven by a keyboard-controlled injector. No hardware involved.

## Run

```
brew install sdl2
pio run -e sim
.pio/build/sim/program                          # data root defaults to ./data
.pio/build/sim/program data rev track 3000      # scenario, start page, capture-and-exit after N ms
.pio/build/sim/program data ota engine 8160     # D03 at 68 % — the planche frame
```

`program [dataRoot] [scenario] [pageId] [captureAfterMs]`. Scenarios are `cruise`,
`rev`, `oil`, `oil-low`, `water`, `warn`, `stale`, `bus-lost`, `cut-boost`,
`cut-knock`, `cut-fuel` and `cut-limp` — the same modes as the keys below — plus
`rev-release` (2 s at the limiter, then back to cruise, so the frame after the
limiter clears is reproducible) and the overlay scenarios `ota`, `ota-complete`,
`ota-failed`, `failure` (pushes two errors so the failure surface renders),
`boot` (D01), `self-test` (D02, with the CAN BUS row failing), `no-config` (D07,
the first-boot empty screen) and `config-rejected` (D06, a layout that does not
fit 240 px). The last two replace the whole screen and hide the dash chrome, so
the captured frame is what a device with no usable config shows. The control
scenarios `controls`, `controls-armed`, `controls-active`, `controls-locked` and
`controls-cruise` pin the signals behind the four button states and synthesise
one tap per button 600 ms in, so armed and active can be captured without the
keyboard, and the splash scenarios `splash-s01` … `splash-s06` (S01 anti-lag
engaged, S02 anti-lag off, S03 launch armed, S04 traction level, S05 ECU map,
S06 refused) pin the signals of the reference frame and re-raise the splash every
300 ms so the takeover is on screen whenever the capture lands. With
`captureAfterMs`
the run writes `sim-screenshot.bmp` and exits, which is how a PR captures a given
frame (e.g. both phases of the 6 Hz rev-limit blink) without touching the keyboard.

## Keys

| Key | Effect |
| --- | --- |
| `C` | cruise scenario (default) — sweeping rpm/speed/boost |
| `R` | rev limiter held at the limit |
| `O` | oil pressure 0.4 bar at 5200 rpm — critical takeover after the 2 s hold |
| `W` | coolant, oil temp and IAT in their warning bands |
| — | `water` scenario only: coolant 118 °C at 4100 rpm — the high-side takeover |
| `1` | boost cut band — warning severity, elapsed counter |
| `2` | ignition retard band — warning severity, `HOLDING` |
| `3` | fuel cut band — danger severity, ink detail |
| `4` | limp mode band — danger severity, `LATCHED` |
| `X` | stop feeding — everything goes stale (`- -` at 500 ms) |
| `B` | D04: feed for 1 s, then drop the bus while battery keeps reading |
| `F` | fake OTA progress overlay (toggle) |
| `←` / `→` | previous / next page |
| `D` | dump the LVGL object tree to stdout |
| `S` | write `sim-screenshot.bmp` |
| `ESC` | quit |

Mouse clicks are the touch input.

## The LVGL pool is the board's

`env:sim` takes `LV_MEM_SIZE` from the board profile through the same
`BOARD_HAS_PSRAM` switch in `include/lv_conf.h` that the hardware builds use.
It stands for the CrowPanel 2.8″, which has no PSRAM, so it gets **80 KB** —
the same 80 KB the panel gets. It used to hardcode 512 KB, which meant a page
could build cleanly here and abort on the board (#213, #214). A green sim now
means something.

Both builds print pool usage at two points, so a capture session shows the
numbers:

```
LVGL  pool after fonts: used=8376/524288 B (1%) frag=0% largest=...
LVGL  pool after first page build: used=122304/524288 B (23%) frag=1% largest=...
```

Today the default config **does not fit the 80 KB build** — `pio run -e sim`
aborts partway through the first page with `lv_mem_realloc: couldn't allocate
memory`, which is #259 reproducing exactly as it does on hardware. Measured in
`sim_psram`, one page plus the shared chrome costs ~122 KB, and that figure is
the same for all six pages, so the overshoot is not about how many pages there
are.

`env:sim_psram` is the opt-in for design work on a page that has not been
trimmed yet. It stands for the S3 boards, which really do have PSRAM, and it
reaches 512 KB through the board switch rather than a number written into
`platformio.ini`. **A green run there proves nothing about a no-PSRAM board.**

```
pio run -e sim_psram && .pio/build/sim_psram/program data rev street 1500
```

## How it works

`sim/` owns `main`, an SDL display/pointer driver, the injector and stdio
implementations of `StorageDriver`, the LVGL `S:` filesystem and the flash
`SPIFFS` shim. `sim/shims/` replaces the Arduino/ESP-IDF/FreeRTOS headers for
the host; `sim_stubs.cpp` stubs the HAL symbols the UI links against
(CAN rate, BLE status, USB lines, touch calibration). Everything under
`src/ui`, `src/config`, `src/runtime` compiles unmodified — the sim renders
the same code the panel runs.
