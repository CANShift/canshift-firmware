# canshift-firmware
<p align="center">
  <img src="../logo/CANShift_firmware_logo.png" alt="Firmware logo" width="600">
</p>

ESP32 firmware for the CANShift automotive dashboard.

**Platform:** Elecrow CrowPanel 2.8" (ESP32, 320×240 ILI9341 + XPT2046 touch)
**Framework:** PlatformIO / Arduino / C++
**UI:** LVGL 8.3
**CAN:** ESP32 TWAI + Adafruit CAN Pal (TJA1051T/3)

---

## What Is Working

- LVGL 8.3 rendering — gauge (numeric / arc / bar), warning, button, gear, image, timer widgets
- ESP32 TWAI CAN reception at 500 kbps
- MaxxECU CAN frame parsing (RPM, throttle, MAP, boost, IAT, coolant, oil temp / pressure, fuel pressure, lambda, AFR, speed, gear, battery, flags)
- Config loaded from SPIFFS JSON at boot (`dashboard.json`, `signals.json`)
- USB serial communication (JSON lines, 115200 baud):
  - `CMD_PUT_CONFIG` — receive new dashboard config, write to SPIFFS, reboot
  - `CMD_SCREEN_SETTINGS` — update brightness, contrast, sleep timeout, rotation
  - `CMD_GET_STATUS` — return firmware version and protocol number
  - `CMD_CAN_SCAN_START` / `CMD_CAN_SCAN_STOP` — forward raw frames to desktop
  - `CMD_REBOOT` — soft reboot
- CAN scan mode — queues raw frames (FreeRTOS queue, 64 frames deep) and drains to USB in 96-byte serialized packets
- CAN health stats — emits `{"can_stat":1,"fps":X.X,"errors":N}` every 2 s to the USB task via volatile struct
- Telemetry push — all live signal values serialized as `{"tele":1,"v":{...}}` every ~200 ms
- Simulation mode — `[env:sim]` generates realistic VR6 data without live ECU
- LVGL draw buffers allocated from DMA heap — avoids DRAM overflow

---

## Build & Flash

### Prerequisites
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) or VS Code + PlatformIO IDE extension
- USB cable to the CrowPanel ESP32

### Commands
```bash
# From canshift-firmware/

pio run                          # Build only
pio run --target upload          # Build and flash firmware
pio run --target uploadfs        # Upload SPIFFS filesystem (config JSON)
pio device monitor               # Serial monitor at 115200 baud

# Simulation mode (no hardware required)
pio run -e sim --target upload
```

### First Flash Checklist
1. Verify all pins in `include/board_config.h` against your CrowPanel 2.8" schematic
2. Set `APP_SIMULATION_MODE 1` in `include/app_config.h` for initial UI test (no CAN hardware needed)
3. `pio run --target upload` — flash firmware
4. `pio run --target uploadfs` — upload config JSON files
5. Confirm display initializes and shows a dashboard page
6. Disable simulation, connect CAN transceiver, verify signal reception
7. Open CANShift Studio, connect via USB, push a config

---

## Folder Structure

```
canshift-firmware/
├── platformio.ini              # Build environments (default, sim)
├── include/
│   ├── board_config.h          # !! All GPIO assignments — verify before flash
│   ├── app_config.h            # Feature flags, task stack sizes, buffer sizes
│   └── lv_conf.h               # LVGL 8.3 compile-time config
├── src/
│   ├── main.cpp                # Entry point — FreeRTOS task creation
│   ├── boot/
│   │   └── boot_sequence.cpp   # Power-on init (HAL, config, UI)
│   ├── hal/
│   │   ├── display/            # TFT_eSPI wrapper (ILI9341, DMA flush)
│   │   ├── touch/              # XPT2046 touch input
│   │   ├── storage/            # SPIFFS read/write
│   │   └── usb/
│   │       ├── usb_comm.h      # USB protocol declarations
│   │       └── usb_comm.cpp    # JSON line parser, command dispatch, CAN scan queue
│   ├── can/
│   │   ├── can_manager.cpp     # TWAI receive loop, CAN health stats emission
│   │   ├── maxxecu_parser.cpp  # MaxxECU CAN frame → signal values
│   │   └── signal_map.h        # Signal ID constants
│   ├── config/
│   │   ├── config_loader.cpp   # SPIFFS JSON → domain structs
│   │   └── config_types.h      # Dashboard / Page / Widget structs (mirrors canshift-core)
│   ├── runtime/
│   │   ├── signal_store.cpp    # Thread-safe live signal value store with timeout
│   │   └── alert_engine.cpp    # Rev limiter flash, warning states
│   ├── ui/
│   │   ├── page_manager.cpp    # Page navigation and lifecycle
│   │   ├── widget_factory.cpp  # Instantiate widgets from config
│   │   ├── settings_page.cpp   # Display settings apply (brightness, rotation, …)
│   │   └── widgets/            # Gauge, bar, warning, button, gear, image, timer
│   ├── sim/
│   │   └── sim_engine.cpp      # Triangle-wave RPM + slow temp ramp for UI dev
│   └── diag/
│       └── logger.cpp          # Leveled LOG_INFO / LOG_WARN / LOG_ERROR macros
└── data/config/
    ├── dashboard.json          # Dashboard layout, pages, widgets, signal bindings
    └── signals.json            # MaxxECU CAN signal definitions (IDs UNVERIFIED)
```

---

## FreeRTOS Task Layout

| Task | Core | Priority | Stack |
|------|------|----------|-------|
| UI task | 1 | 3 | 8192 B |
| CAN task | 0 | 4 | 4096 B |
| USB comm task | 1 | 2 | 4096 B |
| Sim task (sim mode only) | 1 | 1 | 2048 B |

Data flows through `SignalStore` — a central store for live engine values.
The CAN task writes to it after parsing each frame.
The UI task reads from it at each LVGL render tick.
This decouples the CAN receive rate from the UI frame rate.

---

## USB Protocol

JSON lines at 115200 baud. Each message is one JSON object followed by `\n`.

### Commands (desktop → device)

| Cmd | Value | Payload | Behaviour |
|-----|-------|---------|-----------|
| `CMD_PUT_CONFIG` | 0x02 | `{"cmd":2,"payload":{...}}` | Write new `dashboard.json` to SPIFFS, send `{"status":"ok"}`, reboot |
| `CMD_SCREEN_SETTINGS` | 0x05 | `{"cmd":5,"brightness":80,"contrast":50,"sleep":0,"rotation":0}` | Apply display settings via LVGL mutex |
| `CMD_GET_STATUS` | 0x10 | `{"cmd":16}` | Reply: `{"status":"ok","version":"X.Y.Z","protocol":N}` |
| `CMD_CAN_SCAN_START` | 0x20 | `{"cmd":32}` | Enable CAN scan mode, reset queue |
| `CMD_CAN_SCAN_STOP` | 0x21 | `{"cmd":33}` | Disable CAN scan mode |
| `CMD_REBOOT` | 0xF0 | `{"cmd":240}` | `esp_restart()` — no ack sent |

### Proactive output (device → desktop)

| Packet | Rate | Format |
|--------|------|--------|
| Telemetry | ~200 ms | `{"tele":1,"v":{"rpm":3500,"coolant_temp_c":89.2,...}}` |
| CAN frame | Per frame (scan mode) | `{"can":1,"id":888,"len":8,"d":[0,1,2,3,4,5,6,7]}` |
| CAN health | ~2 s | `{"can_stat":1,"fps":125.0,"errors":0}` |

---

## CAN Scan Mode

When `CMD_CAN_SCAN_START` is received:
- `s_canScanMode = true` — all received frames are pushed to a FreeRTOS queue (depth 64, non-blocking)
- The USB task drains up to 32 frames per tick and serializes each as a `can` packet
- The CAN task still parses frames normally — scan mode is additive, not a replacement

---

## CAN Health Stats

The CAN task tracks a 2-second sliding window of received frames.
Every 2 s it calls `UsbComm::updateCanStats(fpsX10, errorCount)` which stores the values in a volatile struct.
On the next `tick()`, the USB task reads the pending stats and emits a `can_stat` packet.
This cross-task communication is intentionally simple (volatile + pending flag) — a stale display value is the worst case.

---

## Simulation Mode

Build with `[env:sim]` or set `APP_SIMULATION_MODE=1` in `platformio.ini`.

- TWAI (CAN) hardware is not initialized
- `SimEngine` generates a triangle-wave RPM sweep and a slow temperature ramp
- All USB communication still works — useful for testing the studio ↔ firmware protocol
- Config loading, page navigation, and widget rendering all function normally

---

## Hardware Assumptions

> Verify all GPIO assignments against your CrowPanel 2.8" schematic before first flash.

| Signal | GPIO | Note |
|--------|------|------|
| TFT SPI MOSI | 13 | |
| TFT SPI MISO | 12 | Write-only display — may be unused |
| TFT SPI SCLK | 14 | |
| TFT CS | 15 | |
| TFT DC/RS | 2 | |
| TFT RST | 4 | May be tied to EN |
| TFT Backlight | 27 | PWM |
| Touch CS | 33 | Shared SPI bus |
| Touch IRQ | 36 | Input-only GPIO |
| TWAI TX | 22 | → CAN Pal CTX |
| TWAI RX | 21 | ← CAN Pal CRX |

CAN speed: 500 kbps — must match MaxxECU CAN output settings.
MaxxECU CAN frame IDs in `signals.json` are **unverified** — confirm in MaxxECU software.

See `include/board_config.h` for all definitions.

---

## Connections to Other Projects

- **canshift-studio** → pushes updated `dashboard.json` over USB, runs CAN scan, triggers firmware updates
- **canshift-core** → defines the JSON schema the firmware config files must comply with (mirrored in `config_types.h`)
- **canshift-mobile** (Phase 2+) → will send config over Wi-Fi / BLE
