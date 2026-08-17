# ECU integration reference

Source: [`src/can/obd2_poller.cpp`](../../src/can/obd2_poller.cpp)

How to wire CANShift to an arbitrary CAN-bus ECU. This is the walkthrough
mentioned in [#842](https://github.com/CANShift/issues/842) /
phase 4 of [#556](https://github.com/CANShift/issues/556) — the
umbrella that drops the historical MaxxECU coupling and makes CANShift
schema-driven against any ECU that broadcasts CAN.

You should be able to complete this in an evening if you already have:

- a copy of your ECU's CAN-output documentation (vendor PDF, DBC file, or
  reverse-engineered notes), and
- hardware wired per [`docs/can-integration-notes.md`](can-integration.md).

If you only own one of the ECUs that ships as a built-in preset
(MaxxECU, OBD-II J1979), skip to [Section 4](#4-pick-a-preset-in-studio-and-flash)
— most of the work is already done.

---

## 1. Locate your ECU's CAN protocol document

Three viable sources, in order of preference:

1. **Vendor spec.** Most aftermarket ECUs publish a CAN output manual. MaxxECU
   ships theirs as a PDF inside the MaxxECU PC software install. Haltech,
   MegaSquirt, Link, and AEM all publish similar documents. The frame IDs +
   byte layouts are exactly what you need.
2. **Community DBC.** For mass-market and OEM ECUs (Bosch ME7, MED9, Subaru,
   etc.), `commaai/opendbc` and `JulianWgs/python-can-decoder` host community
   DBC files. A DBC is the canonical CAN database format — every frame ID,
   byte position, scale/offset already encoded.
3. **Oscilloscope or [CANShift's built-in scanner](#using-the-scanner).** Last
   resort when no documentation exists. The scanner records every frame it
   sees so you can reverse-engineer the bus by physically changing inputs and
   watching which bytes move.

The walkthrough below assumes you've located **frame IDs**, **byte
offsets**, **byte length**, **byte order** (big- vs little-endian),
**signedness**, and **scale / offset** for each signal you care about.

### Using the scanner

CANShift's USB scanner mode emits live frames as JSON over the serial port.
Plug the device into Studio over USB, switch to the **CAN Scanner** tab, and
the bus will surface as `{ "can_frame": { "id": "0x370", "data": "1A2B..." } }`
lines. Even when you have vendor docs, run the scanner for 30 s once — it
catches surprises like frames the docs forgot to mention or rates that don't
match the published cadence.

---

## 2. Translate the protocol doc into a signals catalog

Each signal in `signals.json` is one row in the ECU's CAN protocol table.
The schema is defined in
[`canshift-core/src/schemas/signal.ts`](https://github.com/CANShift/canshift-core/blob/main/src/schemas/signal.ts).
Here is every field, annotated:

```jsonc
{
  // ---- Identity -----------------------------------------------------------
  "name": "rpm", // Signal name. Must match the widget's
  //   `signal` field on the dashboard side.
  //   `canshift-firmware/src/can/signal_map.h`
  //   reserves the well-known names you see
  //   in `SignalIds::*`; new names land there
  //   in lockstep with this catalog.

  // ---- Frame extraction ---------------------------------------------------
  "canFrameId": "0x370", // Frame ID this signal comes from. Hex
  //   string `0x` + 1-3 hex chars.
  "startByte": 0, // 0-based byte offset within the frame
  //   payload (0-7 for an 8-byte CAN frame).
  "byteLength": 2, // 1 / 2 / 4. Bit-packed signals share a
  //   byte: byteLength: 1 + bitMask.
  "bigEndian": true, // true = MSB at `startByte`. Most ECUs use
  //   big-endian; flip this for little-endian.
  "signed": false, // 2's complement interpretation. Coolant
  //   temp + delta-from-target rows are
  //   commonly signed; pressures / rpms are
  //   not.
  "bitMask": "0x01", // OPTIONAL. Single-bit / multi-bit pack.
  //   Hex literal. The decoded value is the
  //   masked + shifted result interpreted as
  //   an integer, then `scale` / `offset`
  //   apply on top.

  // ---- Scaling ------------------------------------------------------------
  "scale": 0.1, // `decoded_value = raw * scale + offset`.
  //   Always emitted as a float, even when 1.
  "offset": -40.0, // Common for temperature signals stored as
  //   °C + 40 to keep them unsigned.

  // ---- Display metadata ---------------------------------------------------
  "unit": "kPa", // Free text. Used by widget label fallback
  //   when `widget.config.suffix` is unset.
  "min": 0, // Display range. NOT a validator — values
  //   outside [min, max] still render but
  //   may clip on bar / arc widgets.
  "max": 400,

  // ---- Alert thresholds (optional) ----------------------------------------
  "warningLevel": 250, // Yellow zone. Strict ordering enforced by
  "dangerLevel": 330, // the schema: warning < danger.
  "highWarningLevel": 15.0, // For two-sided signals like battery V
  "highDangerLevel": 16.0, //   that have low AND high alarm zones.

  // ---- Lifecycle ----------------------------------------------------------
  "timeoutMs": 500, // The firmware's `SignalStore` marks a
  //   signal stale + drops to "invalid" if
  //   no fresh frame lands inside this
  //   window. Pick ≥ 2× the frame's natural
  //   cadence.
}
```

### Bit-packed flag signals

Status flags are usually packed into a single byte of a status frame. Encode
each flag as its own entry with `byteLength: 1` + `bitMask`:

```jsonc
{
  "name": "flag_mil",
  "canFrameId": "0x374",
  "startByte": 0,
  "byteLength": 1,
  "bigEndian": true,
  "signed": false,
  "bitMask": "0x01",              // First bit
  "scale": 1.0, "offset": 0.0,
  "unit": "",
  "min": 0, "max": 1,
  "timeoutMs": 2000
},
{
  "name": "flag_launch_ctrl",
  "canFrameId": "0x374",
  "startByte": 0,
  "byteLength": 1,
  "bigEndian": true,
  "signed": false,
  "bitMask": "0x02",              // Second bit
  "scale": 1.0, "offset": 0.0,
  "unit": "",
  "min": 0, "max": 1,
  "timeoutMs": 2000
}
```

The firmware's diag drawer ([`src/ui/diag_drawer.cpp`](../../src/ui/diag_drawer.cpp))
surfaces these as labelled dots automatically — the names match the
`SignalIds::FLAG_*` constants.

### Outbound frames

If your ECU accepts commands (map switch, button feedback, cruise control),
add an `out` block with the target frame ID:

```jsonc
{
  "out": {
    "map_switch": {
      "id": "0x600",
      "extended": false,
      "encoding": "byte0 = mapIndex (1-based, 1..8)",
    },
  },
}
```

`id` overrides the baked default in
[`canshift-firmware/include/can_signals_out.h`](../../include/can_signals_out.h)
when the firmware loads the catalog at boot (issue #317). `extended: true`
switches to 29-bit framing — auto-promoted when `id > 0x7FF`.

---

## 3. Drop the catalog into a preset (optional but recommended)

If you're integrating an ECU that other users will also want to support,
land a preset in
[`canshift-core/src/ecu-profiles/index.ts`](https://github.com/CANShift/canshift-core/blob/main/src/ecu-profiles/index.ts).
Existing entries (MaxxECU Street, OBD-II J1979) are a literal template:

```ts
export const ECU_PROFILES: EcuProfile[] = [
  // ...
  {
    id: 'mybrand-mymodel',
    name: 'MyBrand MyModel',
    description: 'Frame layout from MyBrand CAN spec rev 4.2.',
    protocol: 'mybrand_v1',
    signals: [
      { name: 'rpm', canFrameId: '0x100', startByte: 0, byteLength: 2, ... },
      // ...
    ],
  },
]
```

[`canshift-core/src/__tests__/ecu-profiles.test.ts`](https://github.com/CANShift/canshift-core/blob/main/src/__tests__/ecu-profiles.test.ts)
validates every preset against the full `SignalConfigSchema` — typo a frame
ID and CI catches it before the preset hits Studio.

If you don't want to share the catalog upstream, skip this step and pick the
`Generic (blank)` preset in Studio, then paste your signals one at a time.

---

## 4. Pick a preset in Studio and flash

1. Open CANShift Studio, connect the device over USB.
2. Open the **Signal Config** route. The dropdown shows every preset from
   `ECU_PROFILES` plus an option to import a custom `signals.json`.
3. Pick your preset (or `Generic (blank)` if you're hand-editing).
4. The preview pane renders every signal in the catalog. Eyeball it — do
   the frame IDs match what your ECU broadcasts? Do the units make sense?
5. **Save** writes the catalog to studio's local `userData/signals.json`.
6. **Push to device** sends the file over USB. The firmware reloads its
   `SignalStore` and the dashboard starts decoding the new layout
   immediately — no reboot needed.

---

## 5. Verify on bench, then on car

### Bench

If you have a CAN bus simulator (Vector VN1610, USBtin, even another ESP32
running a frame injector sketch), feed the device known frames and watch
the dashboard react. The setup screen → dashboard transition will happen
the moment a valid `signals.json` is pushed, even with no live bus.

The diag drawer is your friend here. Swipe-up from the bottom (or tap the
`▴ DIAG` strip) to see:

- ECU flag bits, lit when active.
- The 2×2 status grid (RPM, TPS, coolant, battery).
- Any firmware-level errors (CAN bus off, parse errors, config reload
  failures) accumulated since boot.

### On car

1. Connect CANShift to the ECU's CAN bus via the CAN Pal wired in
   [`docs/can-integration-notes.md`](can-integration.md).
2. Turn key to IGN. The device boots; the dashboard should populate within
   ~1 second of the ECU starting to broadcast.
3. Cross-check at idle: RPM matches your tach, coolant matches the gauge
   cluster, etc.
4. If a signal reads `—` instead of a number:
   - Check the frame ID and byte offsets in `signals.json` — the most
     common error is off-by-one on `startByte`.
   - Check endianness — flipping `bigEndian` swaps high and low bytes.
   - Check `timeoutMs` — too tight and the signal flickers between valid /
     invalid as frames straggle.
5. If a signal is wildly wrong (RPM reads 5× actual, or coolant reads 240 °C
   at idle):
   - The scale is off — either `scale` is wrong by a factor of 10, or the
     ECU uses a different unit (kPa vs psi, °F vs °C) and `offset` doesn't
     compensate.

---

## 6. App notes per shipped preset

Per-ECU integration notes for built-in presets:

| Preset                    | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **MaxxECU Street / Race** | Default frame group at `0x370`-`0x375`. Verify against your MaxxECU's "CAN Output" page in MaxxECU PC software — Street and Race share the layout but Race adds extra frames (race-specific telemetry) you may want to map manually.                                                                                                                                                                                                                       |
| **OBD-II J1979**          | Polling shipped in [#841](https://github.com/CANShift/issues/841). Add a `polling: { mode: 0x01, pid, intervalMs }` block to each signal; the firmware's `Obd2Poller` then sends `0x7DF` requests and decodes the `0x7E8` responses. See `data/signals_obd2_mode01.json.example` for a starter catalog, and the Studio editor's **Signals** tab for the Mode 01 PID picker. v1 scope = Mode 01 + single ECU at `0x7DF/0x7E8`; multi-ECU + ISO-TP deferred. |
| **Generic (blank)**       | Use as the starting point for any ECU not yet covered. No signals predefined — add them via the Studio editor or by editing the exported `signals.json` directly.                                                                                                                                                                                                                                                                                          |

### Want to add a vendor preset?

Open an issue tagged `scope:core` titled `feat(core): <Vendor> <Model>
preset`. Include a screenshot of the relevant page of your ECU's CAN spec
(redact license info if needed). PRs welcome — see
[`canshift-core/src/__tests__/ecu-profiles.test.ts`](https://github.com/CANShift/canshift-core/blob/main/src/__tests__/ecu-profiles.test.ts)
for the validation contract every new preset must clear.

---

## Related

- [#556](https://github.com/CANShift/issues/556) — umbrella: drop the historical MaxxECU coupling.
- [#840](https://github.com/CANShift/issues/840) — phase 1: rename `MaxxEcuParser` → `CanSignalParser`.
- [#19](https://github.com/CANShift/issues/19) — phase 2: preset library this guide depends on.
- [#841](https://github.com/CANShift/issues/841) — phase 3: OBD-II polling.
- [`docs/can-integration-notes.md`](can-integration.md) — physical-layer
  wiring + termination, complementary to this guide.
- [`canshift-firmware/data/config/signals.json`](../../data/config/signals.json)
  — the default catalog the firmware ships with (MaxxECU layout, marked
  unverified pending confirmation against actual ECU output).
