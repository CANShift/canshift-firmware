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
```

`program [dataRoot] [scenario] [pageId] [captureAfterMs]`. Scenarios are `cruise`,
`rev`, `oil` and `stale` — the same modes as the keys below. With `captureAfterMs`
the run writes `sim-screenshot.bmp` and exits, which is how a PR captures a given
frame (e.g. both phases of the 6 Hz rev-limit blink) without touching the keyboard.

## Keys

| Key | Effect |
| --- | --- |
| `C` | cruise scenario (default) — sweeping rpm/speed/boost |
| `R` | rev limiter held at the limit |
| `O` | oil pressure 0.4 bar — critical takeover after the 2 s hold |
| `X` | stop feeding — everything goes stale (`- -` at 500 ms) |
| `F` | fake OTA progress overlay (toggle) |
| `←` / `→` | previous / next page |
| `D` | dump the LVGL object tree to stdout |
| `S` | write `sim-screenshot.bmp` |
| `ESC` | quit |

Mouse clicks are the touch input.

## How it works

`sim/` owns `main`, an SDL display/pointer driver, the injector and stdio
implementations of `StorageDriver`, the LVGL `S:` filesystem and the flash
`SPIFFS` shim. `sim/shims/` replaces the Arduino/ESP-IDF/FreeRTOS headers for
the host; `sim_stubs.cpp` stubs the HAL symbols the UI links against
(CAN rate, BLE status, USB lines, touch calibration). Everything under
`src/ui`, `src/config`, `src/runtime` compiles unmodified — the sim renders
the same code the panel runs.
