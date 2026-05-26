# canshift-firmware

<p align="center">
  <img src="../logo/CANShift_firmware_logo.png" alt="Firmware logo" width="600">
</p>

ESP32 firmware for the CANShift configurable automotive dashboard.

- **Platform:** Elecrow CrowPanel 2.8" (ESP32-WROOM-32, 320×240 ILI9341 + XPT2046 touch)
- **Framework:** PlatformIO + Arduino + C++17
- **UI:** LVGL 8.3 (`lvgl/lvgl @ ^8.3.11`)
- **CAN:** ESP32 TWAI + [Adafruit CAN Pal (TJA1051T/3)](https://www.digikey.ch/fr/products/detail/adafruit-industries-llc/5708/18716420)
- **Wireless:** NimBLE GATT (`h2zero/NimBLE-Arduino @ ^1.4.3`) + optional WiFi softAP for OTA and Studio transport (`links2004/WebSockets @ ^2.7.3` for the WS bridge — #1105)

Library versions are pinned in [`platformio.ini`](platformio.ini) (lines 107–119).

---

## Hardware platform

- **MCU** — ESP32-WROOM-32 mounted on the Elecrow CrowPanel 2.8" ESP32 HMI (SKU `DIS05028H`).
- **Display** — ILI9341, 320×240, SPI bus, backlight on GPIO 27 (PWM channel 0, 5 kHz, 8-bit).
- **Touch** — XPT2046 resistive controller, sharing the display's HSPI bus, polled (no IRQ).
- **CAN** — ESP32 TWAI controller fed through an [Adafruit CAN Pal (TJA1051T/3)](https://www.digikey.ch/fr/products/detail/adafruit-industries-llc/5708/18716420) wired to the CrowPanel expansion header. CAN Pal `CTX → TWAI_TX`, `CRX → TWAI_RX`, `CANH/CANL → ECU CAN H/L`, `VCC → 5 V`, `GND → GND`. CANShift is ECU-agnostic — MaxxECU is the example used during development, but `signals.json` accepts any passively-broadcast frame layout (see #556).

All pin assignments live in [`include/board_config.h`](include/board_config.h) and are still flagged as assumptions until they're verified on the actual board.

---

## What is working

- LVGL 8.3 rendering with widgets for `bar`, `button`, `gauge`, `gear`, `image`, `label`, `timer`, and `warning` (`src/ui/widgets/`).
- Gauge `revFlash` pulse triggered at the configured `revLimitRpm` (see `GaugeWidget::create` in `src/ui/widgets/gauge_widget.cpp`, issue #263).
- Button widgets with toggle latch, optional icon, and idle/active color tints (`src/ui/widgets/button_widget.cpp`).
- ESP32 TWAI CAN reception at 500 kbps, runtime-overridable via `device.json`.
- CAN frame parsing — RPM, throttle, MAP, boost, IAT, coolant, oil temp/pressure, fuel pressure, lambda, AFR, road speed, gear, battery, MIL/launch flags, map number. The shipped default `signals.json` is tuned for MaxxECU but `signals.json` is fully generic; swap it for any passive-broadcast ECU.
- Dynamic CAN signal table built from `signals.json` at boot — runtime dispatch with bitmask support and per-signal timeout.
- Touch calibration via LovyanGFX `calibrateTouch()` (4-point crosshair sequence); result stored in NVS (`namespace="touch"`, `key="cal"`).
- Day/night theme toggle that rebuilds all LVGL pages.
- USB JSON-line protocol over UART0 (115200 baud) — see [USB protocol](#usb-protocol).
- BLE GATT server for the mobile app (`src/hal/ble/ble_server.cpp`) — TELE notify, STATUS read+notify, SETTINGS read+write, CMD channel.
- WiFi softAP started on demand from BLE for future OTA flashing (`src/hal/wifi/wifi_ap.h`); compile-gated by `APP_WIFI_OTA_ENABLED` in `app_config.h`.
- Default-config provisioning — embedded `dashboard.json` / `signals.json` / `theme.json` are written to fresh SPIFFS on first boot. User data is never overwritten (`src/config/default_config.h`, `src/boot/boot_sequence.cpp`).
- Atomic config writes via `StorageDriver::writeFileAtomic` with a `.bak` fallback (see `readAndParseWithBak` in `src/config/config_loader.cpp`).
- Burn overlay — full-screen "Saving config…" feedback with auto error state on storage write failure (`src/ui/burn_overlay.h`).
- Runtime device config (`device.json`) overrides TWAI pins and CAN bus speed without recompiling (see `CanManager::begin` in `src/can/can_manager.cpp`).
- CAN-task IDLE0 yield fix — `vTaskDelay(CAN_TASK_YIELD_TICKS)` keeps the Task Watchdog Timer happy on a busy bus (see `taskCAN` in `src/main.cpp`, issue #200).
- Splash screen with progress bar and a 2 s minimum hold (see `BootSequence::run` in `src/boot/boot_sequence.cpp`).
- CAN scan mode — queues raw frames (FreeRTOS queue, 64 frames deep) and drains them to USB at ≤32 frames per tick.
- CAN health stats emitted as `{"can_stat":1,"fps":X.X,"errors":N}` every 2 s.
- Telemetry push — `{"tele":1,"v":{...}}` every ~200 ms over USB; same payload at 10 Hz over BLE TELE.
- Simulation mode (`[env:sim]`) generates VR6-shaped data with no CAN hardware required.
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

# Dash-hosted Studio (WiFi build) — needs both firmware AND SPIFFS
pio run -e crowpanel_28_wifi --target upload
pio run -e crowpanel_28_wifi --target uploadfs  # mandatory for the SPA (#1123)
```

First boot provisions the embedded default config files automatically — no
manual asset copy step is required. Fonts and icons under `data/assets/` and
`data/fonts/` ship to the device via `pio run -t uploadfs`. On
`[env:crowpanel_28_wifi]`, `data/web/*` also lives on SPIFFS (gzipped SPA
bundle for the dash-hosted Studio, post-#1123 follow-up); without the
`uploadfs` step the dash boots normally but `http://canshift.local/`
returns 404 until the SPIFFS image lands.

### Logging knobs

Two independent log levels gate what reaches UART0 / USB-CDC. They are easy to
confuse because both names contain "level" and both end up on the same wire.

| Knob | Gates | Format | Default in `[env:crowpanel_28]` |
|------|-------|--------|---------------------------------|
| `APP_LOG_LEVEL` | Project `LOG_ERROR` / `LOG_WARN` / `LOG_INFO` / `LOG_DEBUG` / `LOG_VDEBUG` macros (`src/diag/logger.h`) | JSON-line envelope `{"log":1,"lvl":"...","tag":"...","msg":"..."}` (USB protocol v2) | `1` (error only) — release contract from `include/app_config.h` (#899) |
| `CORE_DEBUG_LEVEL` | Arduino-framework `log_e` / `log_w` / `log_i` / `log_d` / `log_v` calls inside arduino-esp32, LovyanGFX, NimBLE, etc. | Plain-text `[E][TAG]…` lines emitted directly by the framework | `1` (errors only) — saves ~4-7 KB flash by stripping framework format strings (#408) |

Levels: `0`=none, `1`=error, `2`=warn, `3`=info, `4`=debug, `5`=verbose. The two
scales line up, but the knobs are otherwise orthogonal — raising one does not
affect the other.

To get full chatter (both framework + app logs at verbose) when debugging,
build `[env:debug]` instead of overriding flags by hand. `[env:sim]` keeps
`APP_LOG_LEVEL=3` + `CORE_DEBUG_LEVEL=4` so the QEMU boot smoke harness sees
the expected info-level boot markers.

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
  0x370000 "canshift-spiffs-${TAG}-crowpanel_28.bin"
```

**Notes:**

- The merged firmware binary already starts at offset `0x0` (it embeds the
  bootloader at its own internal `0x1000` offset). Writing it at `0x1000`
  would shift every component and brick the boot.
- SPIFFS partition offset `0x370000` matches `ota_4mb_wifi.csv` (the
  partition layout shipped since #1117 — see
  [Partition upgrade path](#partition-upgrade-path-1117) below). Dashes
  carrying a pre-#1117 firmware image (SPIFFS at `0x310000`) must be
  re-flashed via USB to pick up the new partition table; OTA between
  layouts is unsafe.
- `460800` baud is reliable on most CH340 + cable combos. If the flash hangs,
  drop to `230400`. Avoid `921600` — it tends to time out on weak cables.
- PlatformIO uses `upload_speed = 115200` by default for cable compatibility
  (`platformio.ini:80`); raise it manually if your cable is reliable.
- After a successful flash the device reboots and the studio auto-connects
  within a few seconds.

---

## Partition upgrade path (#1117)

The partition layout changed in #1117 to make room for the dash-hosted Studio
SPA embedded in `[env:crowpanel_28_wifi]`. Both production (`crowpanel_28`)
and WiFi (`crowpanel_28_wifi`) builds now share the same partition table —
defined in `ota_4mb_wifi.csv` — so an OTA between the two builds is binary-
table compatible.

**What changed:**

| Region        | Before (`ota_4mb.csv`) | After (`ota_4mb_wifi.csv`) |
|---------------|------------------------|----------------------------|
| `app0` / `app1` (each) | 1536 KB @ `0x10000` / `0x190000` | 1856 KB @ `0x10000` / `0x1E0000` |
| `spiffs`      | 832 KB @ `0x310000`    | 512 KB @ `0x370000`        |
| `coredump`    | 128 KB @ `0x3E0000`    | 64 KB @ `0x3F0000`         |

App slots grew by 320 KB each (+640 KB total) so the WiFi build can link
firmware + WebSocket bridge + WiFi/mDNS/lwip stacks + the gzipped Studio SPA
in a single OTA payload. SPIFFS shrank because the runtime content sums to
~220 KB (5 Orbitron .bin fonts ~34 KB + canonical JSON configs ~25 KB + 25
sensor icons ~77 KB + `.bak` atomic-write companions ~25 KB + ~25 % SPIFFS
overhead at small partition sizes ~55 KB) — well under 512 KB with headroom
for future user pushes.

**Field-upgrade story.** Dashes already deployed with a pre-#1117 image
carry the old partition table baked into their bootloader region. An OTA
from the old layout to the new layout is **unsafe** — the running bootloader
writes the new firmware into a slot whose offset / size no longer matches
what the new firmware expects on next boot, and SPIFFS lands in the wrong
region. Until those dashes are USB-reflashed via the standalone
`canshift-flasher`, OTA from a pre-#1117 image is blocked.

Going forward (post-#1117), OTAs between #1117+ images are transparent:
`esp_ota_get_next_update_partition()` reads the live partition table at
runtime, and the table is identical across `crowpanel_28` /
`crowpanel_28_wifi`.

**Known follow-ups (separate PRs):**

- `canshift-studio/src/hooks/useFirmwareFlash.ts` (`0x310000` constant) —
  update to `0x370000` before the bundled Studio flasher can image a #1117+
  build.
- `canshift-flasher/src/constants.ts` (`SPIFFS_FLASH_OFFSET = 0x310000`) —
  same update; this is what end-users will run from `canshift.tmbk.ch`.
- `scripts/secure_boot_first_flash.sh` (`0x310000` argument) — secure-boot
  variant; `ota_4mb_secure.csv` is intentionally unchanged in #1117 so the
  secure-boot flow keeps booting until its own repartition PR lands.
- `.github/workflows/firmware-boot-smoke.yml` (`0x310000` argument to
  `esptool merge_bin`) — QEMU smoke harness will still boot (SPIFFS-mount
  failure is non-fatal pre-`[BOOT] Ready`) but the SPIFFS-resident default
  asset checks downstream will need the new offset.

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
├── platformio.ini                  # Build envs (crowpanel_28, sim, native, …)
├── include/
│   ├── app_config.h                # Feature flags, task sizes, thresholds
│   ├── board.h                     # Compile-time board selector (#831)
│   ├── board_config.h              # !! All GPIO assignments — verify before flash
│   ├── board_profile.h             # Per-board capability flags
│   ├── boards/                     # Per-board pin/feature tables
│   ├── can_signals_out.h           # SignalId-anchored telemetry export table
│   ├── hardware_profile.h          # Hardware feature-flag mirror
│   ├── lgfx_panel.h                # LovyanGFX panel + bus + touch + backlight
│   └── lv_conf.h                   # LVGL 8.3 compile-time config
├── src/
│   ├── main.cpp                    # Entry point — FreeRTOS task creation
│   ├── boot/
│   │   ├── boot_sequence.{cpp,h}   # Power-on init: HAL → LVGL → config → UI
│   │   └── default_fonts.{cpp,h}   # LVGL default-font embed
│   ├── hal/
│   │   ├── ble/ble_server.{cpp,h}  # NimBLE GATT — TELE/STATUS/SETTINGS/CMD/AP_PWD (#873)
│   │   ├── display/                # LovyanGFX wrapper (ILI9341, DMA flush)
│   │   ├── memory/psram.{cpp,h}    # PSRAM probe + allocator helpers
│   │   ├── storage/                # SPIFFS read/write + LVGL FS driver
│   │   ├── touch/                  # XPT2046 — calibrate() + setTouch()
│   │   ├── usb/usb_comm.{cpp,h}    # JSON line parser, command dispatch, scans
│   │   └── wifi/                   # wifi_ap, wifi_tcp, wifi_ws, ota_hmac, ota_hmac_bridge (#667, #827, #1071, #1105)
│   ├── can/
│   │   ├── can_manager.{cpp,h}     # TWAI receive loop, CAN health stats
│   │   ├── can_parser.{cpp,h}      # Runtime signal table from signals.json + fallback
│   │   └── signal_map.{cpp,h}      # Canonical SignalId enum + name table (#279)
│   ├── config/
│   │   ├── config_loader.{cpp,h}   # JSON → domain structs, atomic writes + .bak, PSRAM-backed rollback snapshot (#1073)
│   │   ├── config_types.h          # Mirrors canshift-core schema (C++ structs)
│   │   ├── default_config.{cpp,h}  # First-boot SPIFFS provisioning (embedded JSON)
│   │   └── rotation_config.{cpp,h} # 0°/180° mounting rotation persistence
│   ├── runtime/
│   │   ├── action_dispatcher.{cpp,h} # Button-action → side-effect router (#833)
│   │   ├── alert_engine.{cpp,h}    # Rev-limiter flash, warning state machine
│   │   ├── input_buttons.{cpp,h}   # Physical GPIO button polling (Input task, #833)
│   │   ├── pending_actions.h       # Deferred-action queue used by the dispatcher
│   │   ├── signal_store.{cpp,h}    # Thread-safe live signal store with timeout
│   │   ├── timer_service.{cpp,h}   # Shared monotonic timer abstraction
│   │   └── track_store.{cpp,h}     # Lap/session telemetry buffer (#815)
│   ├── ui/
│   │   ├── alert_flash.{cpp,h}     # Rev-limit screen flash effect
│   │   ├── burn_overlay.{cpp,h}    # "Saving config…" full-screen overlay
│   │   ├── diag_drawer.{cpp,h}     # On-device diagnostics drawer
│   │   ├── error_bar.{cpp,h}       # Config error badge on lv_layer_top
│   │   ├── font_manager.{cpp,h}    # LVGL font registration
│   │   ├── gesture_controller.{cpp,h} # Swipe / multi-tap recognizer
│   │   ├── icon_assets.{cpp,h}     # LVGL icon binary loaders
│   │   ├── page_manager.{cpp,h}    # Page navigation, theme rebuild
│   │   ├── passkey_overlay.{cpp,h} # BLE Secure-Connections passkey display (#873)
│   │   ├── sensor_color_ramp.{cpp,h} # Sensor → ramp resolver (#430)
│   │   ├── sensor_palette.{cpp,h}  # Two-zone sensor palette (#954)
│   │   ├── settings_page.{cpp,h}   # Brightness + sleep + rotation panel
│   │   ├── setup_screen.{cpp,h}    # First-boot setup wizard
│   │   ├── theme_manager.{cpp,h}   # Day/night colour scheme
│   │   ├── top_bar.{cpp,h}         # Status bar — page map, MIL, day/night
│   │   ├── widget_factory.{cpp,h}  # Instantiate widgets from config
│   │   ├── widget_label.{cpp,h}    # Shared widget label primitive
│   │   ├── widget_styles.{cpp,h}   # Reusable lv_style_t bank
│   │   └── widgets/                # bar, button, gauge, gear, image, label,
│   │                               # timer, warning
│   ├── sim/sim_engine.{cpp,h}      # Triangle-wave RPM + slow temp ramp
│   ├── util/                       # format_float, no_float_printf — host-portable helpers
│   └── diag/
│       ├── error_store.{cpp,h}     # Persistent error state for the badge
│       ├── logger.{cpp,h}          # LOG_INFO / LOG_WARN / LOG_ERROR macros
│       ├── lvgl_assert.{cpp,h}     # LVGL invariant asserts
│       ├── lvgl_lock_guard.h       # RAII wrapper around g_lvglMutex
│       └── perf_counters.{cpp,h}   # UI / CAN / heap counters (#1006)
├── data/                           # SPIFFS image — uploaded via `pio run -t uploadfs`
│   ├── config/
│   │   ├── dashboard.json          # Default dashboard layout (also embedded)
│   │   ├── signals.json            # Default CAN signal mapping (MaxxECU example, UNVERIFIED, also embedded)
│   │   └── device.json             # Runtime hardware overrides
│   ├── assets/                     # LVGL .bin icons (sensor_*.bin, etc.)
│   └── fonts/                      # LVGL .bin fonts (orbitron_<weight>_<size>.bin)
└── scripts/
    ├── extra_targets.py            # Inject APP_VERSION_STR + CONFIG_SCHEMA_VERSION + OTA_HMAC_SECRET
    ├── build_rust.py               # Optional Rust HMAC bridge (#827)
    ├── build_sensor_icons.py       # Bulk sensor-icon build
    ├── build_ui_icons.py           # Generic UI-icon build
    ├── generate_keys.sh            # Generate OTA / secure-boot keys
    ├── png_to_lvgl_bin.py          # PNG → LVGL .bin converter
    ├── regen_orbitron_fonts.py     # Rebuild bundled fonts from sources
    └── secure_boot_first_flash.sh  # First-flash secure-boot helper
```

---

## FreeRTOS task layout

| Task | Core | Priority | Stack | Period | Source |
|------|------|----------|-------|--------|--------|
| UI | 1 | 10 | 8192 B | 20 ms (`LVGL_HANDLER_PERIOD_MS`) | `taskUI` — `src/main.cpp` |
| CAN | 0 | 15 | 4096 B | tight loop + `CAN_TASK_YIELD_TICKS` (1 tick) | `taskCAN` — `src/main.cpp` |
| USB | 1 | 8 | 4096 B | 20 ms | `taskUSBComm` — `src/main.cpp` |
| Input | 0 | 7 | 2048 B | poll @ `INPUT_POLL_INTERVAL_MS` | `taskInput` — `src/runtime/input_buttons.cpp` |
| BLE | 1 | 6 | 5120 B | 100 ms (`BLE_TELE_INTERVAL_MS`, ~10 Hz) | `taskBLE` — `src/main.cpp` |
| Sim *(sim mode only)* | 1 | 5 | 2048 B | 50 ms (`SIM_UPDATE_MS`) | `taskSim` — `src/main.cpp` |
| WiFi AP *(OTA on demand)* | 1 | 5 | 4096 B | event-driven | `src/hal/wifi/wifi_ap.cpp` |
| WiFi TCP *(Studio JSON-lines, AP-gated, #1071)* | 1 | 5 | 4096 B | 10 ms | `src/hal/wifi/wifi_tcp.cpp` |
| WiFi WS *(dash-hosted Studio, AP-gated, #1105)* | 1 | 5 | 4096 B | 10 ms | `src/hal/wifi/wifi_ws.cpp` |

Priorities and stack sizes are defined in `include/app_config.h`
(`TASK_PRIO_*` / `TASK_STACK_*` / `TASK_CORE_*` macros — that file is the
canonical source). The LVGL tick is **not** driven by the UI task —
`setup()` installs a periodic `esp_timer` that calls
`lv_tick_inc(LVGL_TICK_MS)` every 5 ms so animations stay wall-clock
accurate even when the UI task overruns. `lv_tick_inc()` is the only LVGL
API documented as mutex-free.

All other LVGL calls require the global `g_lvglMutex` (declared in
`src/main.cpp`). The CAN task writes to `SignalStore`; the UI task reads from
it on each render tick. This decouples the CAN receive rate from the UI frame
rate.

---

## USB protocol

JSON lines at 115200 baud over UART0. Each message is one JSON object followed
by `\n`. `USB_PROTOCOL_VERSION = 2` (defined in `include/app_config.h`). Under
protocol v2, log entries are emitted as `{"log":1,...}` JSON envelopes
(instead of plain `[I][TAG]` text), and all writes are serialized under a
shared UART mutex with command acks.

### Commands (host → device)

Opcodes are defined in `src/hal/usb/usb_comm.h` as `CMD_*` constants.

| Cmd | Name | Payload | Behaviour |
|-----|------|---------|-----------|
| `0x01` | `CMD_GET_CONFIG` | `{"cmd":1}` | Reply with on-disk `dashboard.json`: `{"status":"ok","config":{...}}` (newlines stripped). |
| `0x02` | `CMD_PUT_CONFIG` | `{"cmd":2,"payload":{...}}` | Show burn overlay → atomic storage write → ack → reboot. On failure: `{"status":"error","message":"write_failed"}` and the overlay flips to error state. |
| `0x05` | `CMD_SCREEN_SETTINGS` | `{"cmd":5,"brightness":80,"sleep":0,"rotation":0}` | Apply brightness + sleep via `SettingsPage::applyFromUsb`. If `rotation` (0/180) differs from the current value, persist and reboot. |
| `0x06` | `CMD_PUT_FILE` | `{"cmd":6,"path":"/assets/x.bin","total":N,"idx":i,"data":"<base64>"}` | Chunked, base64 storage write to an allowlisted path prefix (see `kAllowedPutFilePrefixes` in `usb_comm.cpp`). `idx=0` truncates and opens; the final chunk closes the file. Out-of-sequence chunks abort the transfer; idle ≥10 s also aborts. |
| `0x07` | `CMD_TOGGLE_DAY_NIGHT` | `{"cmd":7}` | Flip the day/night theme on the next UI tick. |
| `0x08` | `CMD_CALIBRATE_TOUCH` | `{"cmd":8}` | Run the on-device 4-point crosshair calibration. UI task drives without holding `g_lvglMutex` (calibration blocks on user input). |
| `0x09` | `CMD_SET_DAY_NIGHT` | `{"cmd":9,"day":true\|false}` | Idempotent variant of `0x07` (issue #225). |
| `0x0A` | `CMD_RESET_TOUCH_CAL` | `{"cmd":10}` | Clear the saved touch calibration in NVS; the firmware reverts to the `TOUCH_CAL_*` defaults on the next boot. |
| `0x10` | `CMD_GET_STATUS` | `{"cmd":16}` | Reply: `{"status":"ok","version":"X.Y.Z","protocol":2,"is_day":0\|1}`. |
| `0x20` | `CMD_CAN_SCAN_START` | `{"cmd":32}` | Begin forwarding raw CAN frames; resets the drop counter. |
| `0x21` | `CMD_CAN_SCAN_STOP` | `{"cmd":33}` | Stop forwarding; ack includes `drops`. |
| `0x03` | `CMD_GET_DEVICE_CONFIG` | `{"cmd":3}` | Read `/config/device.json` (TWAI pins + CAN speed); reply `{"status":"ok","config":{...}}`. Wired host-side in studio-web #1118; the firmware dispatcher handler lands alongside this wave (until then the `default` branch acks as a no-op, which the IPC surfaces as `config_not_found`). |
| `0x04` | `CMD_PUT_DEVICE_CONFIG` | `{"cmd":4,"payload":{...}}` | Atomic write of `/config/device.json`; same wire shape as `deviceConfigToWire` in `canshift-core`. Pairs with `0x03`. |
| `0x0B` | `CMD_GET_INPUT_BINDINGS` | `{"cmd":11}` | Read `/config/input_bindings.json` (physical button → action map, #833). Same lifecycle as `0x03`. |
| `0x0C` | `CMD_PUT_INPUT_BINDINGS` | `{"cmd":12,"payload":{...}}` | Atomic write of `/config/input_bindings.json`; pairs with `0x0B`. |

There is no `CMD_REBOOT`, `CMD_PUT_SIGNALS`, or `CMD_PUT_THEME` — older drafts
of this doc listed them. Theme is folded into `dashboard.json` (#901), signals
are pushed via `CMD_PUT_CONFIG` (the firmware re-reads both files atomically),
and reboot is implicit on `CMD_PUT_CONFIG`.

### Proactive output (device → host)

| Packet | Cadence | Format |
|--------|---------|--------|
| Telemetry | 200 ms | `{"tele":1,"v":{"rpm":3500,"coolant_temp_c":89.2,...}}` — only valid (non-stale) signals; full signal name list in `TELE_SIGNALS[]` (`src/hal/usb/usb_comm.cpp`) |
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
  `supervision timeout=4 s` on connect (see `BleServer::onConnect` in
  `src/hal/ble/ble_server.cpp`)

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
1 decimal place (see `addSignalIfValid` in `src/hal/ble/ble_server.cpp`).
Notifications fire only when at least one client is subscribed.

### STATUS payload

```json
{"ver":"X.Y.Z","can":0|1,"is_day":0|1,"ap_ssid":"CANShift-XXXX"}
```

`ap_ssid` is omitted when the AP is inactive. STATUS is refreshed every 2 s
and re-notified on AP-state changes or theme changes (see
`BleServer::publishStatusSnapshot` in `src/hal/ble/ble_server.cpp`).

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

BLE is compiled in for the production build (`APP_BLE_ENABLED=1`) but stays
**off at runtime by default** — `BLE_DEFAULT_ENABLED=0` since #873 so a
freshly-flashed device does not advertise until the user turns BLE on from
the on-device Settings page. The `[env:sim]` build sets `APP_BLE_ENABLED=0`
to keep simulator boots silent. NimBLE adds ~30 KB DRAM; the build flags
trim the stack to peripheral-only.

### Pairing & security (issue #873)

- **Encrypted access control.** All sensitive characteristics (SETTINGS, CMD,
  the AP-password helper) are declared `READ_ENC` / `WRITE_ENC` so NimBLE
  refuses I/O on an unbonded link.
- **Passkey display.** First-time pairing draws a 6-digit passkey on screen
  (see `src/ui/passkey_overlay.cpp`); the phone is prompted to type the same
  digits — MITM-resistant Secure Connections.
- **AP-password characteristic.** The Wi-Fi softAP password is **not**
  embedded in the firmware. The device generates a fresh password at boot
  (or on AP-up), stores it in NVS, and exposes it on a dedicated
  encrypted-read characteristic (`AP_PWD`). Mobile reads it once after
  pairing and stores it via `expo-secure-store`.
- **BLE off by default.** Devices ship with `BLE_DEFAULT_ENABLED=0`; the
  user enables BLE from the on-device Settings page so an unconfigured
  device does not advertise.

---

## Wi-Fi Studio transports (issues #1071, #1105)

When the softAP is up (`APP_WIFI_OTA_ENABLED=1` build, AP started on demand
from BLE) the firmware exposes the USB JSON-lines protocol over two
parallel transports so Studio can connect over the air from either a
native client or a browser. Both share the same `UsbComm::handleLine()`
dispatcher and the same proactive-telemetry stream — only the framing
differs.

### Discovery (mDNS)

`canshift.local` resolves to the softAP IP. Two services are advertised:

| Service              | Port | Path | Transport | Source |
|----------------------|------|------|-----------|--------|
| `_canshift._tcp`     | 5050 | —    | Raw TCP, line-terminated JSON | #1071 (`wifi_tcp.cpp`) |
| `_canshift_ws._tcp`  | 81   | `/`  | WebSocket, one JSON object per text frame | #1105 (`wifi_ws.cpp`) |

The WS service carries a `path=/` TXT record so a discovery client that
sees both can pick the transport it actually supports (browsers can only
use WS).

### Wire protocol

Both transports carry the **same JSON content** as USB. The only
difference is the framing:

| Transport | Framing | Trailing `\n` |
|-----------|---------|----------------|
| USB / TCP | One JSON object per line | Required |
| WS        | One JSON object per **text frame** | None — the WS frame boundary replaces it |

The WS write sink strips the `\n` that USB / TCP responses carry before
calling `sendTXT()`, so a single command-dispatcher implementation drives
all three transports.

### Concurrency & coexistence

- **Single client per transport.** A second TCP connect is rejected at
  `accept()`; a second WS connect is accepted then immediately
  `disconnect()`ed with a `single-client only` reason frame so the peer
  sees a parseable error.
- **TCP and WS coexist.** Both servers run concurrently when the AP is
  up. Telemetry fans out via `UsbComm::setAuxSink` to whichever transport
  connected first (TCP gets priority in the typical "Electron Studio
  joined first, browser tab opened second" case). The second transport
  still receives command responses but not proactive telemetry; defining
  a multi-aux-sink fan-out is a follow-up.
- **Endpoint:** `ws://canshift.local:81/` (or `ws://<dash-ip>:81/`).
  Port 81 instead of 80 because the chosen library
  (`Links2004/arduinoWebSockets`) opens its own listening socket — it
  cannot share port 80 with the OTA HTTP `WebServer` instance. The TCP
  bridge stays at 5050 unchanged.
- **Auth:** WPA2 password on the AP gates both transports. No per-frame
  auth.

### Why a library and not roll-your-own

`Links2004/arduinoWebSockets @ ^2.7.3` is the actively maintained
Arduino-ESP32 WS server (release Jan 2026). The full handshake +
masking + close-frame state machine + ping/pong keepalive is roughly
2 KB of source, and a minimal rewrite would either skip RFC 6455
edge cases or duplicate ~1 KB of code we'd have to maintain. The
library adds ~10 KB Flash + ~768 B BSS at the trimmed
`WEBSOCKETS_SERVER_CLIENT_MAX=2` setting — well under the 8 KB
Flash / 2 KB BSS budget from the #1105 brief.

### DRAM budget — rollback snapshot in PSRAM (#1073)

WiFi + mDNS + lwip + WS pull in ~18 KB of `dram0_0_seg` BSS that the WROOM
DRAM ceiling cannot absorb on top of the LVGL + NimBLE baseline. The
`crowpanel_28_wifi` env reclaims room by allocating
`config_loader`'s ~25 KB transactional rollback snapshot from PSRAM
(via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) instead of holding it in
BSS. The allocation is gated on `BOARD_HAS_PSRAM` and the runtime PSRAM
probe in `src/hal/memory/psram.cpp`. On a WROOM module the probe reports 0
bytes, the alloc returns null, and rollback degrades to a no-op (parse
failures no longer restore prior in-memory state — matches the pre-#458
risk profile, acceptable for the WROOM-only no-WiFi image). Native
unit tests keep the BSS buffer so `pio test -e native` still exercises the
rollback path byte-for-byte.

### Dash-hosted Studio SPA (#1077 phase 4)

When `APP_SPA_SERVE=1` (default on `[env:crowpanel_28_wifi]`), the firmware
serves the browser SPA shipped by `canshift-studio-web/` straight off SPIFFS
through the existing `WebServer` on port 80. The user joins the softAP,
navigates to `http://canshift.local/`, and lands on the Studio
ConnectScreen — no install, no internet. The SPA then talks to the firmware
via the WebSocket transport on port 81 (#1108) for live data.

**Build chain (host side):**

1. `extra_scripts = scripts/sync_studio_web.py` (registered alongside
   `extra_targets.py` on `[env:crowpanel_28_wifi]`) runs `npm run build`
   in `../canshift-studio-web/`, gzip-encodes every text artifact via the
   studio-web post-build hook, then mirrors `dist/*.gz` + `*.woff2` into
   `canshift-firmware/data/web/`.
2. `pio run -t uploadfs` flashes the resulting SPIFFS image to the
   `spiffs` partition. `data/web/*` lands at `/web/*` on the device.
3. `src/hal/wifi/wifi_ap.cpp`'s `kSpaAssets[]` table maps each browser URL
   to its SPIFFS path; the handler opens the file and uses
   `WebServer::streamFile()` to push it back to the browser in 1.4 KB
   chunks. The framework auto-emits `Content-Encoding: gzip` for any
   filename ending in `.gz`, so the `.gz` SPIFFS file extensions
   round-trip transparently.

> **#1123 follow-up — SPA moved out of the firmware embed.** The SPA
> artifacts used to ride in the firmware image via
> `board_build.embed_files`, pushing `[env:crowpanel_28_wifi]` to 107.3 %
> of the 1728 KB app slot at link time. Moving them onto SPIFFS reclaims
> ~185 KB of app-slot flash (now at ~96.8 %). The trade-off: a freshly
> flashed dash needs the extra `pio run -t uploadfs` step (or the
> equivalent SPIFFS image flash from `canshift-flasher`) before the
> dash-hosted Studio loads. `/status`, `/ota`, BLE, and CAN are all
> unaffected — the dash boots and behaves normally either way.

**Source-of-truth list of SPA files** — keep these two in lock-step:

- `canshift-studio-web/scripts/gzip-dist.mjs` (which extensions get a `.gz` sibling)
- `canshift-firmware/scripts/sync_studio_web.py` (`EXPECTED_GZ` + `EXPECTED_FONTS`)
- `canshift-firmware/src/hal/wifi/wifi_ap.cpp` `kSpaAssets[]`

Vite emits hash-free filenames (see `canshift-studio-web/vite.config.ts`)
so the file list stays stable across builds. Cache-busting via content
hash buys nothing here — `Cache-Control: no-store` is set on every
response and the bytes rotate atomically with each uploadfs run.

**Skipping the SPA rebuild:** set `CANSHIFT_SKIP_STUDIO_WEB_BUILD=1` in
the environment to reuse the existing `canshift-studio-web/dist/`. Useful
in CI when a prior job already built the SPA, or when iterating on the
firmware side without touching the SPA.

**First-flash step.** Brand-new dashes carrying a clean `[env:crowpanel_28_wifi]`
firmware image return `404 SPA asset not provisioned (run uploadfs)` for
every `/` and `/assets/*` request until the SPIFFS image is flashed:

```bash
# After `pio run -e crowpanel_28_wifi -t upload` (firmware):
pio run -e crowpanel_28_wifi -t uploadfs
```

The standalone `canshift-flasher` flashes both partitions in one pass via
the merged firmware + SPIFFS bundle published per release, so end-users
joining the dash from a fresh first-flash don't have to think about the
two-step.

**Routes registered when `APP_SPA_SERVE=1`:**

| Method | Path | Content-Type | Content-Encoding | SPIFFS path |
|--------|------|--------------|------------------|-------------|
| GET    | `/`  | `text/html`  | `gzip` (same file as `/index.html`) | `/web/index.html.gz` |
| GET    | `/index.html` | `text/html` | `gzip` | `/web/index.html.gz` |
| GET    | `/assets/index.js`         | `application/javascript` | `gzip` | `/web/assets/index.js.gz` |
| GET    | `/assets/index.css`        | `text/css`               | `gzip` | `/web/assets/index.css.gz` |
| GET    | `/assets/vendor-react.js`  | `application/javascript` | `gzip` | `/web/assets/vendor-react.js.gz` |
| GET    | `/assets/vendor-radix.js`  | `application/javascript` | `gzip` | `/web/assets/vendor-radix.js.gz` |
| GET    | `/assets/vendor-state.js`  | `application/javascript` | `gzip` | `/web/assets/vendor-state.js.gz` |
| GET    | `/assets/EditorRoute.js`   | `application/javascript` | `gzip` | `/web/assets/EditorRoute.js.gz` |
| GET    | `/assets/Orbitron-{Black,Bold,Medium}.woff2` | `font/woff2` | — (already compressed) | `/web/assets/Orbitron-*.woff2` |

All SPA responses carry `Cache-Control: no-store`. The operational
endpoints (`/status`, `/ota`) keep their existing handlers — they're
registered first so the WebServer's exact-match dispatcher checks them
before the SPA routes.

**Flash budget on `_wifi` (post-#1123 follow-up).**

| Configuration | Flash | Notes |
|---|---|---|
| `crowpanel_28` (prod, no SPA, no WiFi libs) | 68.4 % (1.16 MB) | Unchanged by this work |
| `crowpanel_28_wifi` baseline (no SPA) | ~107.3 % (1.81 MB) | Pre-fix overflow — SPA embedded |
| `crowpanel_28_wifi` + SPA on SPIFFS | **~96.8 % (1.63 MB)** | Post-fix — fits the 1728 KB slot |

---

## Wi-Fi OTA (issue #667)

The firmware exposes an HTTP `/ota` endpoint on its softAP for over-the-air
firmware updates. The flow is intentionally minimal — no TLS (the AP has no
cert), but every write is HMAC-authenticated against a per-device bearer
token and an HMAC trailer on the binary itself.

> **Audience: mobile-only.** Studio no longer drives OTA — the dash-hosted
> Studio (`canshift-studio-web/`) is served from the same firmware image it
> would otherwise be updating, and the Electron Studio's flasher is being
> retired in favour of the browser-based USB flasher at
> [canshift.tmbk.ch](https://canshift.tmbk.ch) (separate repo
> [`tburkhalterr/canshift-flasher`](https://github.com/tburkhalterr/canshift-flasher),
> #1081). The mobile app retains the WiFi-OTA path via
> `POST http://192.168.4.1/ota` so a user in the car can update without a
> laptop.

### Per-device bearer token

On AP-up the firmware computes:

```
ota_token = first 16 bytes of SHA-256(ap_password || "ota-bearer-v1")
```

(`OTA_TOKEN_SALT` is defined in `src/hal/wifi/wifi_ap.cpp`; the constant-time
compare lives in `hasValidBearerToken()` in the same file.) The mobile app
reads `ap_password` once over the encrypted BLE `AP_PWD` characteristic and
derives the same token locally; both sides keep it in their respective
secrets stores (NVS on device, `expo-secure-store` on iOS / Android Keystore
on Android). `/ota` rejects every request without a valid bearer.

### HMAC trailer on the binary

The release binary is built with a SHA-256 HMAC trailer appended to the
flash image:

```
[ firmware bytes ........... ][ 32-byte HMAC ]
```

The HMAC is computed over the firmware bytes using `OTA_HMAC_SECRET` (a
shared release-line secret, **not** the per-device bearer above). On
upload the firmware streams the body straight to the OTA flash region,
recomputes the HMAC over the bytes it just wrote, and aborts the swap if
the trailing 32 bytes don't match — preventing accidental flash of an
unsigned or corrupted binary.

### `secrets.ini` build pipeline

`OTA_HMAC_SECRET` is injected at build time by
`scripts/extra_targets.py`. Pipeline:

1. Maintainer creates `canshift-firmware/secrets.ini` (gitignored —
   `secrets.ini.example` is the template) with a real secret.
2. `extra_targets.py` reads it and exposes the value as a build flag.
3. Production build environments (`crowpanel_28_ota`, release) refuse to
   compile if the file is missing or still holds the placeholder string.
4. The CI release workflow passes the same secret via the
   `OTA_HMAC_SECRET` env var so the published binary's trailer matches
   what devices in the wild compiled.

Rotating the secret is therefore a release-line break: pre-rotation
devices reject the new binaries until they're flashed via USB.

---

## Dynamic CAN signal loading

At boot, `ConfigLoader` reads `signals.json` and builds a `RuntimeSignal[]`
table in `can_parser.cpp`. Each entry maps a CAN frame ID and byte offset
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
  `SIGNAL_DEFAULT_TIMEOUT_MS = 1000` ms (defined in `include/app_config.h`).

---

## Touch calibration

`TouchDriver::calibrate()` runs LovyanGFX's built-in `calibrateTouch()`
(4-point crosshair sequence), stores the 10-byte result in NVS, and applies
it via `setTouch()`. On boot, `init()` loads the stored calibration if
present; otherwise it falls back to the `TOUCH_CAL_*` constants in
`board_config.h`.

Calibration can also be triggered remotely:

- USB — `CMD_CALIBRATE_TOUCH` (`0x08`); `CMD_RESET_TOUCH_CAL` (`0x0A`) wipes
  the saved values so the next boot re-calibrates against the defaults.
- BLE — `{"cmd":"start_calibration"}` on the CMD characteristic.

Both run inside the UI task **without** holding `g_lvglMutex` because
`calibrateTouch()` draws via LovyanGFX directly (not LVGL) and blocks on
user input.

---

## Physical buttons (issue #833)

The dash can be driven by physical GPIO buttons in addition to the touchscreen.
The runtime polls every configured pin at 1 kHz on a dedicated Input task
(core 0, priority 7, 2 KB stack — see `app_config.h`), debounces in software,
classifies presses as **short** / **long** / **double**, then dispatches the
resulting event through the same `ActionDispatcher` that touch buttons feed.

### Config — `input_bindings.json`

Optional file at `/config/input_bindings.json` (SPIFFS). When absent, no
bindings are active. Maximum of `MAX_INPUT_BINDINGS` entries (cap defined in
`canshift-core/src/constants/firmware-caps.ts`).

```json
{
  "input_bindings": [
    {
      "id": "btn_cruise_set",
      "pin": 34,
      "active": "low",
      "pullup": true,
      "debounce_ms": 25,
      "kind": "short",
      "action": { "type": "cruise_control", "op": "set" }
    },
    {
      "id": "btn_map_cycle",
      "pin": 35,
      "active": "low",
      "pullup": true,
      "debounce_ms": 25,
      "kind": "short",
      "action": { "type": "map_switch", "delta": 1 },
      "signal": "map_number"
    }
  ]
}
```

Fields:
- **`pin`** — ESP32 GPIO number. Allowlist enforced by
  `Esp32InputGpioSchema` (`canshift-core/src/schemas/device.ts`): all
  output-safe pins **plus** 34-39 (input-only). Pins 6-11 (SPI flash) are
  rejected — they'd brick the device.
- **`active`** — `"low"` (default, button to GND + internal pullup) or
  `"high"` (external pulldown, button to 3.3 V).
- **`pullup`** — `true` enables the ESP32's internal pull-up resistor.
  Required when `active: "low"` unless an external pull-up is fitted.
  **Pins 34-39 have no internal pull-up** — they need an external resistor.
- **`debounce_ms`** — software debounce, bounded by
  `DEBOUNCE_MIN_MS` / `DEBOUNCE_MAX_MS` in core. 20-50 ms covers most
  mechanical buttons.
- **`kind`** — `"short"` (release within ~600 ms), `"long"` (held past
  ~600 ms), or `"double"` (two short presses within ~400 ms).
- **`action`** — same shape as a dashboard button widget's `action` field.
  Supported types: `navigate_page`, `map_switch`, `can_raw`, `cruise_control`.
  Adding a new action type only requires extending `ButtonActionSchema` in
  `canshift-core`; the firmware dispatcher picks it up automatically.
- **`signal`** *(optional)* — when set, the physical button shares its visual
  toggle state with every on-screen button widget bound to the same signal
  name. Pressing a physical "ALS arm" button flips the on-screen ALS button's
  active tint without waiting for the ECU echo. Either side can disarm.

### Wiring

Default pattern: button between GPIO and **GND**, internal pull-up enabled.
Released = `HIGH`, pressed = `LOW` → `active: "low"`.

For input-only pins (34-39) the internal pull-up doesn't exist; wire an
external 10 kΩ pull-up to 3.3 V and use `pullup: false`.

### Editing in Studio

Studio's `InputBindingsSection` lets you add / edit / remove bindings; pin
pickers consume `SAFE_INPUT_PINS_WROOM32` from `canshift-core` so an invalid
pin is rejected before the push to the dash.

---

## Simulation mode

Build with `[env:sim]` (see `platformio.ini`) or set `APP_SIMULATION_MODE=1`
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

CAN speed: 500 kbps default (defined in `board_config.h`); runtime override
via `device.json`. The default `signals.json` shipped with the firmware is a
MaxxECU layout and the frame IDs in it are **unverified** — confirm them
against your ECU's CAN protocol document (the MaxxECU PC software for that
default, or your ECU vendor's docs for any other layout) before relying on
the readings.

### Powering an external CAN transceiver

GPIO 25 and GPIO 32 sit on the CrowPanel 2.8" expansion header but the
header itself does **not** expose a dedicated 3.3 V rail next to them. The
SN65HVD230 / TJA1051 / MCP2562 module needs power — source it from the
expansion-header pin labelled `GPIO_D` on the silkscreen, which is wired to
the board's 3.3 V regulator output (observed on the dash revision shipped in
2026-05). A 100 nF decoupling cap right at the transceiver's VCC pin is
recommended.

Footprints `R35` and `C21` (silkscreen) are present on the board for
pull-up / decoupling on the CAN pair; verify they match your transceiver
module's terminator + decoupling before final assembly — board revisions may
ship them unpopulated.

---

## Config files on SPIFFS

Each canonical config lives at `/config/` on the SPIFFS partition (paths from
`board_config.h`).

| File | Purpose | Required? |
|------|---------|-----------|
| `dashboard.json` | Layout, pages, widgets, signal bindings, day theme | Provisioned from the firmware embed on first boot |
| `signals.json` | CAN signal mapping (default: MaxxECU example) | Same — provisioned on first boot |
| `device.json` | Runtime hardware overrides (TWAI pins, CAN speed) | Optional — falls back to `board_config.h` |
| `input_bindings.json` | Physical GPIO button → action map (#833) | Optional — falls back to no bindings |

The standalone `theme.json` file was removed in schema 1.13 → 1.14 (#901);
day-theme palette lives under `dashboard.json.dayTheme`. Older firmware
images that still wrote `theme.json` are migrated automatically the next
time Studio pushes a config.

In addition, `/assets/*.bin` holds icon images in LVGL binary format,
generated by `scripts/png_to_lvgl_bin.py`.

Every canonical config is written via `StorageDriver::writeFileAtomic`, which
keeps a `<file>.bak` companion. On boot, the loader falls back to `.bak` if
the primary file is missing or corrupt (see `readAndParseWithBak` in
`src/config/config_loader.cpp`).

---

## Connections to other workspaces

- **canshift-studio-web** (dash-hosted Studio) — served straight from this
  package via `board_build.embed_files` + `kSpaAssets[]` in `wifi_ap.cpp`
  on port 80; live data + commands flow over the WebSocket transport on
  port 81 (`wifi_ws.cpp`, #1108). Same `UsbComm::handleLine()` dispatcher
  as USB.
- **canshift-studio** (Electron, legacy) — pushes `dashboard.json` over USB
  (`CMD_PUT_CONFIG`), streams asset files (`CMD_PUT_FILE`), runs CAN scans.
  Kept until phase 3 of #1077 cuts over to the dash-hosted Studio in
  production. The bundled Web-Serial flasher path is being retired in
  favour of the standalone `canshift-flasher` (#1081); update
  `useFirmwareFlash.ts`'s `0x310000` constant to `0x370000` before it can
  image a #1117+ build (tracked as a follow-up).
- **canshift-mobile** — connects over BLE; reads telemetry, writes settings,
  triggers the WiFi softAP for OTA, and uploads firmware via
  `POST /update` on the AP. Pairs with the `ble_server.cpp` characteristics
  described above. Independent of both Studio flavours.
- **canshift-flasher** (separate repo) — browser-based esptool hosted at
  [canshift.tmbk.ch](https://canshift.tmbk.ch). First-flash, recovery,
  and pre-#1117 partition-layout migration. Reads the merged firmware +
  SPIFFS images from the GitHub release feed.
- **canshift-core** — owns the JSON schema; the firmware mirrors it in
  `src/config/config_types.h`. `CONFIG_SCHEMA_VERSION` is injected at build
  time from `canshift-core/src/index.ts` by `scripts/extra_targets.py`, the
  same script that injects `APP_VERSION_STR` from
  `canshift-studio/package.json` (kept as the version source until the
  Electron package retires).

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
[`CLAUDE.md`](../CLAUDE.md). `clang-format` **is** enforced by CI
(`.github/workflows/ci.yml` — `firmware — clang-format check`), and the
host-side Unity native test suite runs on every firmware PR via the
`firmware — native tests` job. Run `pio run -t format` (or the local
`clang-format -i` invocation it wraps) before pushing. The pin assignments
in `board_config.h` are still flagged "verify before flash"; please file an
issue with any board-level discoveries.

---

## License

See the root [`LICENSE`](../LICENSE).
