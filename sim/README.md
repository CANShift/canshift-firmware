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

## Panel size

The window follows the runtime board profile, which defaults to the compile-time
board — 320×240, the `base` display tier. `CANSHIFT_SIM_BOARD` points at a board
profile JSON and overrides it, so the tiers no shipped board reaches yet can be
rendered and captured:

```
CANSHIFT_SIM_BOARD=sim/boards/sim_medium_480x320.json .pio/build/sim/program data cruise street 1500
CANSHIFT_SIM_BOARD=sim/boards/sim_large_800x480.json  .pio/build/sim/program data cruise street 1500
```

The first line of output names the resolved tier. Zoom drops to 1 past 640 px so
the window still fits on a laptop. These profiles are simulator fixtures, not
supported boards — they are deliberately absent from `include/boards/` and
`.github/boards.json`.

A page authored for `base` is scaled onto the larger panel, and the value faces
stop at 84 px, so a large capture shows boxes that grew and type that did not —
that ceiling is the font ladder in #64.

`program [dataRoot] [scenario] [pageId] [captureAfterMs]`. Scenarios are `cruise`,
`rev`, `oil`, `oil-low`, `water`, `warn`, `stale`, `bus-lost`, `cut-boost`,
`cut-knock`, `cut-fuel` and `cut-limp` — the same modes as the keys below — plus
`rev-release` (2 s at the limiter, then back to cruise, so the frame after the
limiter clears is reproducible) and the overlay scenarios `ota`, `ota-complete`,
`ota-failed`, `failure` (pushes two errors so the failure surface renders),
`boot` (D01), `self-test` (D02, with the CAN BUS row failing), `no-config` (D07,
the first-boot empty screen) and `config-rejected` (D06, a layout that does not
fit 240 px), and `workshop` (the workshop-mode card, entered through the real
path: credentials generated into the in-memory NVS shim, pages released). The
last three replace the whole screen and hide the dash chrome, so
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

On hardware the LVGL pool is sized at run time: 512 KB in PSRAM when the module
has it, 80 KB of internal RAM otherwise ([Build flags](../docs/reference/build-flags.md#the-lvgl-pool-and-its-failure-path)).
The sim stands in for a PSRAM board: `env:sim` declares `BOARD_HAS_PSRAM`, so
LVGL's own pool resolves to **512 KB** in `include/lv_conf.h`, and `SIM_BUILD`
keeps it on plain `malloc`.

Both builds print pool usage at two points, so a capture session shows the
numbers:

```
LVGL  pool after fonts: used=8376/524288 B (1%) frag=0% largest=...
LVGL  pool after first page build: used=69696/524288 B (13%) frag=1% largest=...
```

To see the WROOM budget in the sim, build it without `BOARD_HAS_PSRAM`: the
pool drops to the 80 KB a no-PSRAM board gets.


## How it works

`sim/` owns `main`, an SDL display/pointer driver, the injector and stdio
implementations of `StorageDriver`, the LVGL `S:` filesystem and the flash
`SPIFFS` shim. `sim/shims/` replaces the Arduino/ESP-IDF/FreeRTOS headers for
the host; `sim_stubs.cpp` stubs the HAL symbols the UI links against
(CAN rate, BLE status, USB lines, touch calibration). Everything under
`src/ui`, `src/config`, `src/runtime` compiles unmodified — the sim renders
the same code the panel runs.
