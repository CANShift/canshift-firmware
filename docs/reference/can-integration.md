# CAN integration notes

Source: [`src/can/can_manager.cpp`](../../src/can/can_manager.cpp)

## Hardware Overview

```
Your ECU
    │ CANH / CANL
    ▼
Adafruit CAN Pal (TJA1051T/3)
    │ CTX → ESP32 GPIO 22 (TWAI TX, CrowPanel 2.8")
    │ CRX → ESP32 GPIO 21 (TWAI RX, CrowPanel 2.8")
    │ VCC → 5V
    │ GND → GND
    ▼
ESP32 TWAI controller
```

### CAN Pal (TJA1051T/3) Notes

- 5V supply rail (connect to board 5V)
- 3.3V logic compatible on TX/RX — ESP32 GPIO is directly compatible
- Supports up to 1 Mbps
- Integrated wake-up filter
- TXD dominant timeout protection (prevents bus lock if ESP32 hangs)

### Termination

- A CAN bus requires exactly two 120 Ω termination resistors, one at each end
- Check your ECU's documentation — some have internal termination, some don't
- The CAN Pal does NOT include termination — add a 120 Ω resistor on its CANH-CANL pins if needed
- Short stubs (<30 cm) typically work without extra termination

---

## Signal Mapping

### Important

The frame IDs and byte positions in `signals.json` are **examples**.
Verify them against your ECU's CAN output configuration before use.

**How to verify:**

1. Use Studio's **CAN Scanner** tab — connect the display while the ECU is running
2. Capture live frames: note which IDs appear and at what rate
3. Cross-reference with your ECU's documentation or configuration software
4. Update `signals.json` to match, then push from Studio

### Example Frame Layout (edit to match your ECU)

| Frame ID | Example content                      |
| -------- | ------------------------------------ |
| 0x370    | RPM, TPS, MAP, IAT, Speed            |
| 0x371    | Lambda, Gear, Fuel pressure          |
| 0x372    | Coolant temp, Oil temp, Oil pressure |
| 0x373    | Battery voltage                      |
| 0x374    | Status flags (MIL, launch, etc.)     |
| 0x375    | Map number / profile                 |

### CAN Speed

Default: **500 kbps** — configurable in `signals.json` (`canSpeedKbps`) and Studio → Device Config.
Common ECU values: 500 kbps, 1 Mbps. Verify in your ECU's configuration.

---

## Update Rates

Different signals typically update at different rates. Set `timeoutMs` in `signals.json`
to at least 3× the expected update period to avoid false "signal lost" warnings.

| Signal category | Typical update rate |
| --------------- | ------------------- |
| RPM, TPS, MAP   | 10 ms (100 Hz)      |
| Speed, Lambda   | 20 ms (50 Hz)       |
| Temperatures    | 100 ms (10 Hz)      |
| Oil pressure    | 50 ms (20 Hz)       |
| Flags / status  | 100 ms (10 Hz)      |

---

## Signal Smoothing Strategy

Gauges (RPM, boost, oil pressure) use EMA (Exponential Moving Average) smoothing
to prevent LVGL widget jitter from high-frequency signal noise:

```
smoothed = α × raw + (1 - α) × smoothed
```

Where `α = SIGNAL_EMA_ALPHA = 0.2` (from `app_config.h`).

- Lower α = more smoothing (slower response to sudden changes)
- Higher α = less smoothing (faster response, more jitter)

Labels showing temperature, voltage, gear use raw values (no smoothing needed).

---

## Diagnostics

CAN health can be monitored via the USB serial console:

```
[I][CAN] TWAI driver started successfully
[I][CAN] Frame count: 1234
[W][CAN] TWAI receive error: ESP_ERR_TIMEOUT
[E][CAN] TWAI bus-off — attempting recovery
```

If you see constant timeout errors:

1. Check CAN speed matches ECU setting
2. Check CANH/CANL wiring (not swapped)
3. Check termination
4. Confirm ECU CAN output is enabled

If you see bus-off errors:

1. Check for wiring short
2. Check for ground loop between ECU and display power

---

## Known Risks

1. **Frame IDs unverified** — `signals.json` contains example frame IDs.
   If wrong, CAN data will be silently ignored. Always verify with Studio's CAN Scanner.

2. **TWAI pin conflict** — the two pins the board profile claims for TWAI (22 and 21 on the
   CrowPanel 2.8") must not be used by SPI or other peripherals.
   Verify against `include/boards/<your board>.h` and your board's schematic.

3. **CAN ground** — ensure a common ground between ECU, CAN Pal, and ESP32.
   Poor grounding causes signal integrity issues and CRC errors.

4. **Noise** — in a car environment, CAN bus noise is common.
   Use twisted pair for CANH/CANL wiring. Keep CAN cable away from ignition wiring.
