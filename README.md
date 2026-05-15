# canshift-firmware

<p align="center">
  <img src="../logo/CANShift_firmware_logo.png" alt="Firmware logo" width="600">
</p>

ESP32 firmware for the CANShift configurable automotive dashboard.

- **Platform:** Elecrow CrowPanel 2.8" (ESP32-WROOM-32, 320×240 ILI9341 + XPT2046 touch)
- **Framework:** PlatformIO + Arduino + C++17
- **UI:** LVGL 8.3 (`lvgl/lvgl @ ^8.3.11`)
- **CAN:** ESP32 TWAI + Adafruit CAN Pal (TJA1051T/3)
- **Wireless:** NimBLE GATT (`h2zero/NimBLE-Arduino @ ^1.4.3`) + optional WiFi softAP for OTA

Library versions are pinned in [`platformio.ini`](platformio.ini) (lines 107–119).

---

## Hardware platform

- **MCU** — ESP32-WROOM-32 mounted on the Elecrow CrowPanel 2.8" ESP32 HMI (SKU `DIS05028H`).
- **Display** — ILI9341, 320×240, SPI bus, backlight on GPIO 27 (PWM channel 0, 5 kHz, 8-bit).
- **Touch** — XPT2046 resistive controller, sharing the display's HSPI bus, polled (no IRQ).
- **CAN** — ESP32 TWAI controller fed through an Adafruit CAN Pal (TJA1051T/3) wired to the CrowPanel expansion header. CAN Pal `CTX → TWAI_TX`, `CRX → TWAI_RX`, `CANH/CANL → ECU CAN H/L`, `VCC → 5 V`, `GND → GND`.

All pin assignments live in [`include/board_config.h`](include/board_config.h) and are still flagged as assumptions until they're verified on the actual board.

---

## What is working

- LVGL 8.3 rendering with widgets for `bar`, `button`, `gauge`, `gear`, `image`, `label`, `timer`, and `warning` (`src/ui/widgets/`).
- Gauge `revFlash` pulse triggered at the configured `revLimitRpm` (`src/ui/widgets/gauge_widget.cpp:261-270`, issue #263).
- Button widgets with toggle latch, optional icon, and idle/active color tints (`src/ui/widgets/button_widget.cpp`).
- ESP32 TWAI CAN reception at 500 kbps, runtime-overridable via `device.json`.
- CAN frame parsing — RPM, throttle, MAP, boost, IAT, coolant, oil temp/pressure, fuel pressure, lambda, AFR, road speed, gear, battery, MIL/launch flags, map number.
- Dynamic CAN signal table built from `signals.json` at boot — runtime dispatch with bitmask support and per-signal timeout.
- Touch calibration via TFT_eSPI's 4-point crosshair routine; result stored in NVS (`namespace="touch"`, `key="cal"`).
- Day/night theme toggle that rebuilds all LVGL pages.
- USB JSON-line protocol over UART0 (115200 baud) — see [USB protocol](#usb-protocol).
- BLE GATT server for the mobile app (`src/hal/ble/ble_server.cpp`) — TELE notify, STATUS read+notify, SETTINGS read+write, CMD channel.
- WiFi softAP started on demand from BLE for future OTA flashing (`src/hal/wifi/wifi_ap.h`); compile-gated by `APP_WIFI_OTA_ENABLED` in `app_config.h`.
- Default-config provisioning — embedded `dashboard.json` / `signals.json` / `theme.json` are written to fresh SPIFFS on first boot. User data is never overwritten (`src/config/default_config.h`, `src/boot/boot_sequence.cpp`).
- Atomic config writes via `StorageDriver::writeFileAtomic` with a `.bak` fallback (`src/config/config_loader.cpp:48-99`).
- Burn overlay — full-screen "Saving config…" feedback with auto error state on storage write failure (`src/ui/burn_overlay.h`).
- Runtime device config (`device.json`) overrides TWAI pins and CAN bus speed without recompiling (`src/can/can_manager.cpp:58-69`).
- CAN-task IDLE0 yield fix — `vTaskDelay(CAN_TASK_YIELD_TICKS)` keeps the Task Watchdog Timer happy on a busy bus (`src/main.cpp:248`, issue #200).
- Splash screen with progress bar and a 2 s minimum hold (`src/boot/boot_sequence.cpp:85-112,222-330`).
- CAN scan mode — queues raw frames (FreeRTOS queue, 64 frames deep) and drains them to USB at ≤32 frames per tick.
- CAN health stats emitted as `{"can_stat":1,"fps":X.X,"errors":N}` every 2 s.
- Telemetry push — `{"tele":1,"v":{...}}` every ~200 ms over USB; same payload at 10 Hz over BLE TELE.
- Simulation mode (`[env:sim]`) generates synthetic engine data with no CAN hardware required.
- LVGL draw buffers sized at 20 lines × 320 px × 2 bytes (~12.8 KB each) so the firmware fits comfortably alongside NimBLE in DRAM.

---

## Build & flash

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) or VS Code + the PlatformIO IDE extension
- A USB cable to the CrowPanel ESP32

### Commands

```bash
# From canshift-firmware/

pio run                          # Build only
pio run --target upload          # Build and flash firmware
pio run --target uploadfs        # Upload SPIFFS filesystem (data/)
pio device monitor               # Serial monitor at 115200 baud

# Simulation mode (no hardware required)
pio run -e sim --target upload
```

First boot provisions the embedded default config files automatically — no
manual asset copy step is required. Fonts and icons under `data/assets/` and
`data/fonts/` ship to the device via `pio run -t uploadfs`.

### Flashing a release

There are two supported paths:

1. **CANShift Studio firmware updater (recommended).** Open Settings → Firmware
   Update → Check for update. The studio bundles `esptool` and handles port
   detection, baud, and the SPIFFS partition automatically.
2. **Manual `esptool` flash (fallback).** Use this when the studio isn't
   available, or to flash a binary that hasn't been published yet.

**One-time setup** for the manual path:

```bash
# Option A — Homebrew
brew install esptool

# Option B — pip (any platform)
pip install esptool
```

**Manual flash procedure** — works on macOS, Linux, and Windows (or WSL):

```bash
# 1. Download the latest firmware + SPIFFS bundle from
#    https://github.com/tburkhalterr/CANShift/releases
#    Files needed:
#      canshift-firmware-vX.Y.Z-crowpanel_28-merged.bin
#      canshift-spiffs-vX.Y.Z-crowpanel_28.bin

# 2. Identify the device's serial port. With the device plugged in:
#    macOS:    ls /dev/tty.usbserial-*
#    Linux:    ls /dev/ttyUSB*
#    Windows:  Check Device Manager → Ports (COM & LPT)

# 3. Flash both partitions in one command (replace PORT and TAG):
PORT=/dev/tty.usbserial-10
TAG=v0.7.0

esptool.py --chip esp32 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode keep --flash_size keep --flash_freq keep \
  0x0      "canshift-firmware-${TAG}-crowpanel_28-merged.bin" \
  0x310000 "canshift-spiffs-${TAG}-crowpanel_28.bin"
```

**Notes:**

- The merged firmware binary already starts at offset `0x0` (it embeds the
  bootloader at its own internal `0x1000` offset). Writing it at `0x1000`
  would shift every component and brick the boot.
- SPIFFS partition offset `0x310000` matches `partitions/ota_4mb.csv`.
- `460800` baud is reliable on most CH340 + cable combos. If the flash hangs,
  drop to `230400`. Avoid `921600` — it tends to time out on weak cables.
- PlatformIO uses `upload_speed = 115200` by default for cable compatibility
  (`platformio.ini:80`); raise it manually if your cable is reliable.
- After a successful flash the device reboots and the studio auto-connects
  within a few seconds.

---

## First-flash checklist

1. Verify pins in [`include/board_config.h`](include/board_config.h) **and** the
   contents of [`data/config/device.json`](data/config/device.json) against
   your CrowPanel 2.8" schematic and your CAN-Pal wiring.
2. Set `APP_SIMULATION_MODE=1` in `include/app_config.h` (or build `[env:sim]`)
   for the very first UI test — it skips CAN init and disables BLE.
3. `pio run -e sim --target upload` — flash the simulation build.
4. Confirm the display initializes, the splash holds for 2 s, and a dashboard
   page renders with simulated data.
5. Drop simulation, switch back to `[env:crowpanel_28]`, connect the CAN
   transceiver, and verify signal reception via the studio's CAN scan.
6. Confirm `device.json` matches your CAN-Pal wiring (`twai_tx_pin`,
   `twai_rx_pin`, `can_speed_kbps`). If absent, the firmware falls back to
   `PIN_TWAI_TX` / `PIN_TWAI_RX` from `board_config.h`.
7. Open CANShift Studio, connect via USB, and push a config.

---

## Folder structure

```
canshift-firmware/
├── platformio.ini                  # Build envs (crowpanel_28, sim, debug)
├── include/
│   ├── board_config.h              # !! All GPIO assignments — verify before flash
│   ├── app_config.h                # Feature flags, task sizes, thresholds
│   ├── hardware_profile.h          # Board capability flags
│   ├── lgfx_panel.h                # LovyanGFX panel + bus + touch + backlight
│   └── lv_conf.h                   # LVGL 8.3 compile-time config
├── src/
│   ├── main.cpp                    # Entry point — FreeRTOS task creation
│   ├── boot/
│   │   └── boot_sequence.{cpp,h}   # Power-on init: HAL → LVGL → config → UI
│   ├── hal/
│   │   ├── display/                # LovyanGFX wrapper (ILI9341, DMA flush)
│   │   ├── touch/                  # XPT2046 — calibrate() + setTouch()
│   │   ├── storage/                # SPIFFS read/write + LVGL FS driver
│   │   ├── usb/usb_comm.{cpp,h}    # JSON line parser, command dispatch, scans
│   │   ├── ble/ble_server.{cpp,h}  # NimBLE GATT — TELE / STATUS / SETTINGS / CMD
│   │   └── wifi/wifi_ap.{cpp,h}    # On-demand softAP for OTA (compile-gated)
│   ├── can/
│   │   ├── can_manager.{cpp,h}     # TWAI receive loop, CAN health stats
│   │   ├── maxxecu_parser.{cpp,h}  # Runtime signal table from signals.json + fallback
│   │   ├── signal_map.{cpp,h}      # Canonical SignalId enum + name table (#279)
│   ├── config/
│   │   ├── config_loader.{cpp,h}   # JSON → domain structs, atomic writes + .bak
│   │   ├── config_types.h          # Mirrors canshift-core schema (C++ structs)
│   │   ├── default_config.{cpp,h}  # First-boot SPIFFS provisioning (embedded JSON)
│   │   └── rotation_config.{cpp,h} # 0°/180° mounting rotation persistence
│   ├── runtime/
│   │   ├── signal_store.{cpp,h}    # Thread-safe live signal store with timeout
│   │   └── alert_engine.{cpp,h}    # Rev-limiter flash, warning state machine
│   ├── ui/
│   │   ├── page_manager.{cpp,h}    # Page navigation, theme rebuild
│   │   ├── theme_manager.{cpp,h}   # Day/night colour scheme
│   │   ├── widget_factory.{cpp,h}  # Instantiate widgets from config
│   │   ├── top_bar.{cpp,h}         # Status bar — page map, MIL, day/night
│   │   ├── settings_page.{cpp,h}   # Brightness + sleep + rotation panel
│   │   ├── burn_overlay.{cpp,h}    # "Saving config…" full-screen overlay
│   │   ├── alert_flash.{cpp,h}     # Rev-limit screen flash effect
│   │   ├── error_bar.{cpp,h}       # Config error badge on lv_layer_top
│   │   ├── font_manager.{cpp,h}    # LVGL font registration
│   │   ├── icon_assets.{cpp,h}     # LVGL icon binary loaders
│   │   ├── widget_label.h          # Shared widget label primitive
│   │   └── widgets/                # bar, button, gauge, gear, image, label,
│   │                               # timer, warning
│   ├── sim/sim_engine.{cpp,h}      # Triangle-wave RPM + slow temp ramp
│   └── diag/
│       ├── logger.{cpp,h}          # LOG_INFO / LOG_WARN / LOG_ERROR macros
│       └── error_store.{cpp,h}     # Persistent error state for the badge
├── data/                           # SPIFFS image — uploaded via `pio run -t uploadfs`
│   ├── config/
│   │   ├── dashboard.json          # Default dashboard layout (also embedded)
│   │   ├── signals.json            # CAN signal mapping — edit to match your ECU (also embedded)
│   │   ├── theme.json              # Default theme overrides (also embedded)
│   │   └── device.json             # Runtime hardware overrides
│   ├── assets/                     # LVGL .bin icons (sensor_*.bin, etc.)
│   └── fonts/                      # LVGL .bin fonts (orbitron_<weight>_<size>.bin)
└── scripts/
    ├── extra_targets.py            # Inject APP_VERSION_STR + CONFIG_SCHEMA_VERSION
    ├── png_to_lvgl_bin.py          # PNG → LVGL .bin converter
    └── build_sensor_icons.py       # Bulk sensor-icon build
```

---

## FreeRTOS task layout

| Task | Core | Priority | Stack | Period | Source |
|------|------|----------|-------|--------|--------|
| UI | 1 | 10 | 8192 B | 20 ms (`LVGL_HANDLER_PERIOD_MS`) | `taskUI` — `src/main.cpp:163` |
| CAN | 0 | 15 | 4096 B | tight loop + `CAN_TASK_YIELD_TICKS` (1 tick) | `taskCAN` — `src/main.cpp:241` |
| USB | 1 | 8 | 4096 B | 20 ms | `taskUSBComm` — `src/main.cpp:257` |
| BLE | 1 | 6 | 5120 B | 100 ms (`BLE_TELE_INTERVAL_MS`, ~10 Hz) | `taskBLE` — `src/main.cpp:272` |
| Sim *(sim mode only)* | 1 | 5 | 2048 B | 50 ms (`SIM_UPDATE_MS`) | `taskSim` — `src/main.cpp:289` |

Priorities and stack sizes come from `include/app_config.h` (lines 38–69 and
186–197). The LVGL tick is **not** driven by the UI task — `setup()` installs a
periodic `esp_timer` that calls `lv_tick_inc(LVGL_TICK_MS)` every 5 ms so
animations stay wall-clock accurate even when the UI task overruns
(`src/main.cpp:46-61`). `lv_tick_inc()` is the only LVGL API documented as
mutex-free.

All other LVGL calls require the global `g_lvglMutex` (declared in
`src/main.cpp`). The CAN task writes to `SignalStore`; the UI task reads from
it on each render tick. This decouples the CAN receive rate from the UI frame
rate.

---

## USB protocol

JSON lines at 115200 baud over UART0. Each message is one JSON object followed
by `\n`. `USB_PROTOCOL_VERSION = 2` (`include/app_config.h:219`). Under
protocol v2, log entries are emitted as `{"log":1,...}` JSON envelopes
(instead of plain `[I][TAG]` text), and all writes are serialized under a
shared UART mutex with command acks.

### Commands (host → device)

| Cmd | Name | Payload | Behaviour |
|-----|------|---------|-----------|
| `0x01` | `CMD_GET_CONFIG` | `{"cmd":1}` | Reply with on-disk `dashboard.json`: `{"status":"ok","config":{...}}` (newlines stripped). `src/hal/usb/usb_comm.cpp:435-459` |
| `0x02` | `CMD_PUT_CONFIG` | `{"cmd":2,"payload":{...}}` | Show burn overlay → atomic storage write → ack → reboot. On failure: `{"status":"error","message":"write_failed"}` and the overlay flips to error state. `usb_comm.cpp:172-227` |
| `0x03` | `CMD_PUT_SIGNALS` | reserved | Falls through to the default handler (acks `ok`); no dedicated handler yet. |
| `0x04` | `CMD_PUT_THEME` | reserved | Same as above. |
| `0x05` | `CMD_SCREEN_SETTINGS` | `{"cmd":5,"brightness":80,"sleep":0,"rotation":0}` | Apply brightness + sleep via `SettingsPage::applyFromUsb`. If `rotation` (0/180) differs from the current value, persist and reboot. `usb_comm.cpp:331-358` |
| `0x06` | `CMD_PUT_FILE` | `{"cmd":6,"path":"/assets/x.bin","total":N,"idx":i,"data":"<base64>"}` | Chunked, base64 storage write. `idx=0` truncates and opens; the final chunk closes the file. Out-of-sequence chunks abort the transfer; idle ≥10 s also aborts. `usb_comm.cpp:264-329` |
| `0x07` | `CMD_TOGGLE_DAY_NIGHT` | `{"cmd":7}` | Flip the day/night theme on the next UI tick. `usb_comm.cpp:463-467` |
| `0x08` | `CMD_CALIBRATE_TOUCH` | `{"cmd":8}` | Run the on-device 4-point crosshair calibration. UI task drives without holding `g_lvglMutex` (calibration blocks on user input). `usb_comm.cpp:481-485` |
| `0x09` | `CMD_SET_DAY_NIGHT` | `{"cmd":9,"day":true\|false}` | Idempotent variant of `0x07` (issue #225). `usb_comm.cpp:468-480` |
| `0x10` | `CMD_GET_STATUS` | `{"cmd":16}` | Reply: `{"status":"ok","version":"X.Y.Z","protocol":2,"is_day":0\|1}`. `usb_comm.cpp:417-433` |
| `0x20` | `CMD_CAN_SCAN_START` | `{"cmd":32}` | Begin forwarding raw CAN frames; resets the drop counter. `usb_comm.cpp:486-493` |
| `0x21` | `CMD_CAN_SCAN_STOP` | `{"cmd":33}` | Stop forwarding; ack includes `drops`. `usb_comm.cpp:494-504` |
| `0xF0` | `CMD_REBOOT` | `{"cmd":240}` | `esp_restart()` after the default ack. |

### Proactive output (device → host)

| Packet | Cadence | Format |
|--------|---------|--------|
| Telemetry | 200 ms | `{"tele":1,"v":{"rpm":3500,"coolant_temp_c":89.2,...}}` — only valid (non-stale) signals; full signal name list in `TELE_SIGNALS[]` (`usb_comm.cpp:51-69`) |
| CAN frame *(scan mode)* | per frame, drained ≤32/tick | `{"can":1,"id":888,"len":8,"d":[0,1,2,3,4,5,6,7]}` |
| CAN health | 2 s | `{"can_stat":1,"fps":125.0,"errors":0}` |
| Log entry *(protocol v2)* | event-driven | `{"log":1,"lvl":"info","tag":"USB","msg":"..."}` |
| Command ack | per command | `{"status":"ok"}` or `{"status":"error","message":"<reason>"}` |

---

## BLE protocol

NimBLE GATT, peripheral-only build (central / observer / broadcaster / mesh
roles disabled in `platformio.ini:97-104` to keep flash + DRAM in budget).

- **Service UUID:** `4fa0b6a0-0000-0000-0000-000000000001`
- **Device name:** `CANShift`
- **TX power:** `ESP_PWR_LVL_P9` (max)
- **Connection params:** server requests `min=15 ms`, `max=30 ms`,
  `supervision timeout=4 s` on connect (`src/hal/ble/ble_server.cpp:88`)

### Characteristics

| UUID | Properties | Direction | Payload |
|------|------------|-----------|---------|
| `4fa0b6a0-0000-0000-0000-000000000002` | READ + NOTIFY | device → app | TELE — live telemetry, ~10 Hz |
| `4fa0b6a0-0000-0000-0000-000000000003` | READ + NOTIFY | device → app | STATUS — version + CAN health + WiFi AP |
| `4fa0b6a0-0000-0000-0000-000000000004` | READ + WRITE | bidirectional | SETTINGS — brightness / sleep / rotation |
| `4fa0b6a0-0000-0000-0000-000000000005` | WRITE + WRITE_NR | app → device | CMD — device commands |

### TELE — compact JSON keys

| Key | Signal | Unit |
|-----|--------|------|
| `r` | RPM | rpm |
| `tps` | throttle position | % |
| `map` | manifold pressure | kPa |
| `bst` | boost | bar |
| `iat` | intake-air temp | °C |
| `ct` | coolant temp | °C |
| `ot` | oil temp | °C |
| `op` | oil pressure | bar |
| `fp` | fuel pressure | bar |
| `lam` | lambda | λ |
| `s` | road speed | kph |
| `g` | gear | int |
| `bat` | battery | V |

Only valid (non-stale) signals are included. Values are rounded server-side to
1 decimal place (`ble_server.cpp:64-66`). Notifications fire only when at
least one client is subscribed.

### STATUS payload

```json
{"ver":"X.Y.Z","can":0|1,"is_day":0|1,"ap_ssid":"CANShift-XXXX"}
```

`ap_ssid` is omitted when the AP is inactive. STATUS is refreshed every 2 s
and re-notified on AP-state changes or theme changes (`ble_server.cpp:68-78,
270-281`).

### SETTINGS payload

A read returns the current values; a write applies them.

```json
{"brightness":80,"sleep":30,"rotation":0|180}
```

Rotation is applied only when the value differs from the current setting,
because applying it triggers a reboot.

### CMD payload

| `cmd` value | Extra fields | Behaviour |
|-------------|--------------|-----------|
| `start_wifi_ap` | — | Bring up softAP `CANShift-XXXX`, push STATUS notify |
| `stop_wifi_ap` | — | Tear down the AP, push STATUS notify |
| `toggle_day_night` | — | Flip theme on next UI tick |
| `set_day_night` | `"day": true\|false` | Idempotent set |
| `start_calibration` | — | Run on-device touch calibration |
| `reboot` | — | `esp_restart()` after 100 ms |

BLE is gated by `APP_BLE_ENABLED` (default `1`, force `0` in `[env:sim]` —
see `platformio.ini:133`). NimBLE adds ~30 KB DRAM; the build flags trim
the stack to peripheral-only.

---

## Dynamic CAN signal loading

At boot, `ConfigLoader` reads `signals.json` and builds a `RuntimeSignal[]`
table in `maxxecu_parser.cpp`. Each entry maps a CAN frame ID and byte offset
to a `SignalId` with scale, offset, endianness, sign, and optional bit mask.

- `parseFrame()` iterates the runtime table first; unmatched frames fall
  through to the hardcoded switch for backward compatibility. Signals not
  present in `signals.json` use the hardcoded fallback values.
- Frame IDs are parsed from the `canFrameId` field — hex strings are
  supported, e.g. `"0x370"`.
- `bitMask` is a `uint8_t` covering a single byte for flag-type signals
  (e.g. `flag_mil`, `flag_launch_ctrl`).
- `signal_map.h` defines the canonical `SignalId` enum used by `SignalStore`
  and the BLE / USB telemetry tables. Names in `signals.json` must match the
  single source of truth (issue #279).
- Per-signal `timeoutMs` controls `SignalStore::isValid()`. The fallback is
  `SIGNAL_DEFAULT_TIMEOUT_MS = 1000` ms (`include/app_config.h:106`).

---

## Touch calibration

`TouchDriver::calibrate()` runs TFT_eSPI's built-in `calibrateTouch()` (4-point
crosshair sequence), stores the 10-byte result in NVS, and applies it via
`setTouch()`. On boot, `init()` loads the stored calibration if present;
otherwise it falls back to the `TOUCH_CAL_*` constants in `board_config.h`.

Calibration can also be triggered remotely:

- USB — `CMD_CALIBRATE_TOUCH` (`0x08`).
- BLE — `{"cmd":"start_calibration"}` on the CMD characteristic.

Both run inside the UI task **without** holding `g_lvglMutex` because
`calibrateTouch()` draws via TFT_eSPI directly (not LVGL) and blocks on user
input (`src/main.cpp:170-181`).

---

## Simulation mode

Build with `[env:sim]` (`platformio.ini:127-134`) or set `APP_SIMULATION_MODE=1`
in your build flags. In sim mode:

- TWAI hardware is not initialized.
- `SimEngine` generates a triangle-wave RPM sweep and a slow temperature ramp.
- USB protocol still works end-to-end.
- Config loading, page navigation, and widget rendering all behave normally.
- BLE and WiFi are auto-disabled (`-DAPP_BLE_ENABLED=0` in `[env:sim]`).

---

## Hardware assumptions

> Verify all GPIO assignments against your CrowPanel 2.8" schematic before the
> first flash. Source: [`include/board_config.h`](include/board_config.h).

| Signal | GPIO | Note |
|--------|------|------|
| TFT MOSI | 13 | |
| TFT MISO | 12 | Display is write-only |
| TFT SCLK | 14 | |
| TFT CS | 15 | |
| TFT DC/RS | 2 | |
| TFT RST | -1 | Held high internally on CrowPanel 2.8" |
| TFT Backlight | 27 | PWM channel 0, 5 kHz, 8-bit |
| Touch CS | 33 | Shared SPI bus |
| Touch IRQ | -1 | Polled via `getTouch()` |
| TWAI TX | **25** | → CAN Pal CTX |
| TWAI RX | **32** | ← CAN Pal CRX |

CAN speed: 500 kbps default (`board_config.h:89`); runtime override via
`device.json`. Frame IDs in `signals.json` are examples — verify them against
your ECU's CAN output configuration.

---

## Config files on SPIFFS

Each canonical config lives at `/config/` on the SPIFFS partition (paths from
`board_config.h`).

| File | Purpose | Required? |
|------|---------|-----------|
| `dashboard.json` | Layout, pages, widgets, signal bindings, day theme | Provisioned from the firmware embed on first boot (`platformio.ini`) |
| `signals.json` | CAN signal mapping (edit to match your ECU) | Same — provisioned on first boot |
| `theme.json` | Default theme overrides | Optional; provisioned with the embed defaults |
| `device.json` | Runtime hardware overrides (TWAI pins, CAN speed) | Optional — falls back to `board_config.h` |

In addition, `/assets/*.bin` holds icon images in LVGL binary format,
generated by `scripts/png_to_lvgl_bin.py`.

Every canonical config is written via `StorageDriver::writeFileAtomic`, which
keeps a `<file>.bak` companion. On boot, the loader falls back to `.bak` if
the primary file is missing or corrupt (`src/config/config_loader.cpp:48-99`).

---

## Connections to other workspaces

- **canshift-studio** — pushes `dashboard.json` over USB (`CMD_PUT_CONFIG`),
  streams asset files (`CMD_PUT_FILE`), runs CAN scans, and flashes firmware
  through its bundled `esptool` integration.
- **canshift-mobile** — connects over BLE; reads telemetry, writes settings,
  and can trigger the WiFi softAP for OTA. Pairs with the `ble_server.cpp`
  characteristics described above.
- **canshift-core** — owns the JSON schema; the firmware mirrors it in
  `src/config/config_types.h`. `CONFIG_SCHEMA_VERSION` is injected at build
  time from `canshift-core/src/index.ts` by `scripts/extra_targets.py`, the
  same script that injects `APP_VERSION_STR` from
  `canshift-studio/package.json`.

---

## CI — boot smoke-test gate

Every PR touching `canshift-firmware/` runs a QEMU boot smoke test that
asserts the production binary reaches `[BOOT] Ready` within 30 s exactly
once, with no panic strings (`Guru Meditation`, `abort()`, `assert failed`,
`Backtrace:`, multiple `rst:0x`). Do not remove the `[BOOT] Ready` log
line at the end of `BootSequence::run()` in `src/boot/boot_sequence.cpp`
— CI depends on it. Workflow: `.github/workflows/firmware-boot-smoke.yml`
(issue #486).

## Contributing & issues

Bug reports, feature requests, and hardware-verification PRs are welcome at
[`github.com/tburkhalterr/CANShift`](https://github.com/tburkhalterr/CANShift).
Commit messages follow Conventional Commits — see the project root
[`CLAUDE.md`](../CLAUDE.md). `pio check` and `clang-format` are not enforced
in CI but are reasonable local hygiene steps. The pin assignments in
`board_config.h` are still flagged "verify before flash"; please file an
issue with any board-level discoveries.

---

## License

See the root [`LICENSE`](../LICENSE).
