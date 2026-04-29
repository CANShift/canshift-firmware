# canshift-firmware
<p align="center">
  <img src="../logo/CANShift_firmware_logo.png" alt="Firmware logo" width="600">
</p>

ESP32 embedded firmware for the configurable automotive dashboard.

**Platform:** Elecrow CrowPanel 2.8" (ESP32, 320×240, ILI9341 + XPT2046)
**Framework:** PlatformIO / Arduino / C++
**UI:** LVGL 8.3
**CAN:** ESP32 TWAI + Adafruit CAN Pal (TJA1051T/3)

---

## Current Status

Phase 1 skeleton generated. The following is in place:
- PlatformIO project configured (`platformio.ini`)
- Hardware pin definitions isolated in `include/board_config.h`
- LVGL 8.3 configuration in `include/lv_conf.h`
- Application feature flags in `include/app_config.h`
- Modular C++ source structure in `src/`
- Full HAL layer stubs (display, touch, CAN/TWAI, storage, USB)
- CAN parser architecture for MaxxECU CAN protocol
- Runtime signal store with timeout handling
- Alert engine (rev limiter flash, warning states)
- UI layer: page manager, widget factory, top bar
- Simulation mode (no live ECU required)
- Example JSON configs in `data/config/`

**Not yet validated on real hardware.** All hardware-specific pin assignments
must be confirmed against your actual CrowPanel 2.8" board before first flash.
See the "Hardware Assumptions" section below.

---

## Folder Structure

```
canshift-firmware/
├── platformio.ini              # PlatformIO build config
├── include/
│   ├── board_config.h          # !! ALL hardware pin assignments — verify before flash
│   ├── app_config.h            # Feature flags, task config, buffer sizes
│   ├── hardware_profile.h      # Board capability definitions
│   ├── display_config.h        # LVGL display resolution and timing
│   └── lv_conf.h               # LVGL 8.3 compile-time configuration
├── src/
│   ├── main.cpp                # Boot entry point, FreeRTOS task setup
│   ├── boot/
│   │   ├── boot_sequence.h
│   │   └── boot_sequence.cpp   # Power-on init sequence
│   ├── hal/                    # Hardware Abstraction Layer
│   │   ├── display/            # ILI9341 / TFT_eSPI driver wrapper
│   │   ├── touch/              # XPT2046 touch driver wrapper
│   │   ├── can/                # ESP32 TWAI driver wrapper
│   │   ├── storage/            # SPIFFS / SD filesystem wrapper
│   │   └── usb/                # USB serial communication (phase 1 config sync)
│   ├── can/                    # CAN decode layer
│   │   ├── can_manager.h/cpp   # TWAI task, frame dispatch
│   │   ├── maxxecu_parser.h/cpp# MaxxECU protocol parser
│   │   └── signal_map.h        # Signal ID registry
│   ├── config/                 # Config loading and domain types
│   │   ├── config_loader.h/cpp # JSON → domain model
│   │   └── config_types.h      # Dashboard, Page, Widget, Signal structs
│   ├── runtime/                # Live data and state
│   │   ├── signal_store.h/cpp  # Live signal values, timeout tracking
│   │   └── alert_engine.h/cpp  # Warning logic, rev limiter flash
│   ├── ui/                     # LVGL UI layer
│   │   ├── page_manager.h/cpp  # Page navigation, lifecycle
│   │   ├── widget_factory.h/cpp# Widget instantiation from config
│   │   ├── top_bar.h/cpp       # Top status bar (map name, icons)
│   │   ├── theme_manager.h/cpp # Day/night theme application
│   │   └── widgets/            # Individual widget implementations
│   │       ├── base_widget.h
│   │       ├── gauge_widget.h/cpp
│   │       ├── label_widget.h/cpp
│   │       ├── warning_widget.h/cpp
│   │       └── button_widget.h/cpp
│   ├── sim/                    # Simulation engine
│   │   └── sim_engine.h/cpp    # Generates fake signal values for UI dev
│   └── diag/                   # Diagnostics and logging
│       └── logger.h/cpp        # Leveled logging over Serial
├── data/                       # Filesystem data (uploaded to SPIFFS)
│   ├── config/
│   │   ├── dashboard.json      # Dashboard layout and widget definitions
│   │   ├── signals.json        # CAN signal mapping (MaxxECU)
│   │   └── theme.json          # Color and style theme
│   └── assets/
│       └── icons/              # PNG/BMP icons for widgets (place files here)
└── docs/
    └── hardware-assumptions.md # Pin mapping rationale and verification checklist
```

---

## Build & Flash

### Prerequisites
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) installed
- Or [VS Code + PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
- USB cable connected to CrowPanel ESP32

### Commands
```bash
# From the canshift-firmware/ directory:

# Build only
pio run

# Build and upload
pio run --target upload

# Upload filesystem (config JSON files in data/)
pio run --target uploadfs

# Serial monitor (115200 baud)
pio device monitor

# Build + upload + monitor
pio run --target upload && pio device monitor
```

### First Flash Checklist
1. [ ] Verify all pins in `include/board_config.h` match your board
2. [ ] Set `APP_SIMULATION_MODE 1` in `include/app_config.h` for initial UI test
3. [ ] `pio run --target upload` — firmware
4. [ ] `pio run --target uploadfs` — JSON config files
5. [ ] Confirm display initializes and shows a page
6. [ ] Disable simulation, connect CAN, verify signal reception
7. [ ] Verify touch input works

---

## Hardware Assumptions

> **These must be verified against your actual CrowPanel 2.8" board before first flash.**
> The Elecrow CrowPanel 2.8" product line has had minor hardware revisions.

| Signal         | Assumed GPIO | Note                              |
|----------------|-------------|-----------------------------------|
| TFT SPI MOSI   | GPIO 13     | Verify against board silk screen  |
| TFT SPI MISO   | GPIO 12     | May be unused (write-only display)|
| TFT SPI SCLK   | GPIO 14     | Verify                            |
| TFT CS         | GPIO 15     | Verify                            |
| TFT DC/RS      | GPIO 2      | Verify                            |
| TFT RST        | GPIO 4      | Verify — may be tied to EN        |
| TFT BL         | GPIO 27     | Backlight PWM — verify            |
| Touch SPI MOSI | GPIO 13     | Shared SPI bus                    |
| Touch SPI MISO | GPIO 12     | Shared SPI bus                    |
| Touch SPI SCLK | GPIO 14     | Shared SPI bus                    |
| Touch CS       | GPIO 33     | Verify                            |
| Touch IRQ      | GPIO 36     | Input-only GPIO — verify          |
| TWAI TX        | GPIO 22     | CAN Pal TX input                  |
| TWAI RX        | GPIO 21     | CAN Pal RX output                 |
| SD CS          | GPIO 5      | If SD slot present — verify       |
| SD MOSI        | GPIO 23     | May use second SPI bus            |
| SD MISO        | GPIO 19     | Verify                            |
| SD SCLK        | GPIO 18     | Verify                            |

**CAN Transceiver Wiring (Adafruit CAN Pal):**
```
CAN Pal CANH → MaxxECU CAN H
CAN Pal CANL → MaxxECU CAN L
CAN Pal CTX  → ESP32 GPIO 22 (TWAI TX)
CAN Pal CRX  → ESP32 GPIO 21 (TWAI RX)
CAN Pal VCC  → 5V
CAN Pal GND  → GND
```

**CAN Bus Termination:**
The MaxxECU already has internal termination.
Do NOT add a second 120Ω termination unless you are extending the bus significantly.

See `docs/can-integration-notes.md` for full CAN wiring and protocol details.

---

## Firmware Architecture

```
[boot_sequence] ─── init HAL ─── init config ─── init UI ─── start tasks

FreeRTOS Tasks:
  [canTask]        reads TWAI frames → maxxecu_parser → signal_store
  [uiTask]         LVGL tick + rendering, reads signal_store
  [alertTask]      monitors signal_store → alert_engine → UI overlays
  [usbCommTask]    listens for config sync requests from desktop app
  [simTask]        (sim mode only) writes fake values to signal_store
```

Data flows through `SignalStore` — a thread-safe central store for live engine values.
The UI reads from `SignalStore` at its render tick. CAN parsing writes to it.
This decouples the CAN rate from the render rate.

---

## Simulation Mode

Set `#define APP_SIMULATION_MODE 1` in `include/app_config.h`.

In simulation mode:
- CAN hardware is not initialized
- `SimEngine` generates realistic VR6 engine data (RPM sweep, temperature ramp, etc.)
- All UI features work normally
- Useful for UI development, layout validation, and demo purposes

---

## Configuration Files

All runtime configuration is loaded from SPIFFS at boot:

| File                       | Purpose                                           |
|----------------------------|---------------------------------------------------|
| `data/config/dashboard.json` | Pages, widgets, layout, signal bindings         |
| `data/config/signals.json`   | CAN frame IDs, signal bit positions, scaling    |
| `data/config/theme.json`     | Colors, fonts, styles for day/night modes       |

See `docs/config-contract.md` for the full schema specification.

---

## What Remains To Validate On Hardware

- [ ] Exact pin assignments for CrowPanel 2.8" (display, touch, SD)
- [ ] LVGL display flush callback timing (TFT_eSPI DMA vs polling)
- [ ] Touch calibration values for XPT2046
- [ ] TWAI GPIO selection (avoid pins used by SPI)
- [ ] SPIFFS partition size is adequate for config + assets
- [ ] CAN bus baud rate matches MaxxECU settings (default 500kbps or 1Mbps)
- [ ] MaxxECU CAN frame IDs and signal byte positions (verify in MaxxECU software)
- [ ] Rev limiter RPM value (MaxxECU Street street tune specific)
- [ ] Memory usage — LVGL frame buffers vs available PSRAM

---

## Connections to Other Projects

- **canshift-studio** → sends updated JSON config over USB serial
- **canshift-core** → defines the JSON schema the firmware config files must comply with
- **canshift-mobile** (future) → will send config over Wi-Fi/BLE

---

## Resume Work From Here

1. Install PlatformIO and open `canshift-firmware/` as the project root
2. Edit `include/board_config.h` — verify every pin against your board schematic
3. Build with simulation mode ON: confirm display initializes
4. Upload filesystem: `pio run --target uploadfs`
5. Inspect serial log for boot messages
6. Verify touch input registers correct screen coordinates
7. Disable simulation, wire CAN transceiver, verify signal parsing
8. Implement real widget render logic in `src/ui/widgets/`
9. Test full page navigation flow
