# Add a new ECU profile

How to add CANShift support for a new ECU's CAN protocol — from finding the
documentation through to shipping the catalog entry. Almost all of this is data
authoring in `canshift-core`; the firmware needs no changes.

The companion [ECU integration reference](../reference/ecu-integration.md)
is the deep field-by-field schema reference; this guide is the phased
walkthrough that points at it.

## Overview

CANShift is **ECU-agnostic by design**. The firmware does not know what a
"MaxxECU" is — it loads `signals.json` at boot and decodes every CAN frame that
matches a configured entry. Adding a new ECU is overwhelmingly a data-authoring
task: write the right rows in `signals.json`, no firmware changes.

There are two integration paths depending on how the ECU talks:

1. **Broadcast ECU** — the ECU continuously sends frames on its own. This is the
   default mode for aftermarket ECUs: MaxxECU, Haltech, Link, AEM, MegaSquirt,
   Adaptronic. Authoring is "translate the CAN protocol PDF into JSON". No
   firmware code path beyond the regular passive decoder. This covers the large
   majority of ECUs.
2. **Request/response (OBD-II Mode 01)** — the ECU only answers when polled.
   CANShift sends a query frame on `0x7DF` and decodes the response from
   `0x7E8`. Mode 01 only in v1; multi-ECU + ISO-TP are deferred to OBD-II v2.

The built-in presets today are `generic-blank`, `maxxecu-street`, and
`obd2-j1979`
([`ecu-profiles/index.ts`](https://github.com/CANShift/canshift-core/blob/main/src/ecu-profiles/index.ts)).
If your ECU is already one of them, skip straight to step 3 — pick it in the
tuner and flash.

## Step 1 — Identify the ECU's wire format

You need, for every signal you want on the dash:

- **CAN frame ID** — the 11-bit (or 29-bit) arbitration ID the ECU sends the
  signal on. Hex string `0x123` in the schema.
- **Byte offset** — 0-based position within the 8-byte payload.
- **Byte length** — 1, 2, or 4 bytes (the only values the schema accepts).
- **Byte order** — big-endian (MSB first, most ECUs) or little-endian.
- **Signedness** — does the ECU use two's complement?
- **Scale + offset** — `real_value = raw * scale + offset`. Temperature signals
  commonly use `offset: -40` so they fit unsigned bytes.
- **Unit** — free text, displayed by widgets when no manual override is set.

Where to find this:

1. **Vendor CAN protocol document.** Best source. MaxxECU ships theirs in the PC
   software install. Haltech, Link, AEM, etc. publish PDFs on their websites.
2. **Community DBC file.** For mass-market and OEM ECUs: `commaai/opendbc`,
   `JulianWgs/python-can-decoder`. A DBC is the canonical encoding; every field
   above is already there.
3. **Vendor CAN XML.** Some tuning tools export a custom CAN XML for hundreds of
   ECUs. CANShift imports these directly via
   [`parseCanXml`](https://github.com/CANShift/canshift-core/blob/main/src/can-xml/parse-can-xml.ts)
   (the parser recognises the `<RealDashCAN` root tag), and the tuner surfaces
   it through the
   [`XmlImportZone`](https://github.com/CANShift/canshift-tuner/blob/main/src/components/ecu/XmlImportZone.tsx).
   Less reverse-engineering than reading a PDF.
4. **Live scanner.** As a last resort, use CANShift's USB scanner mode
   (`CMD_CAN_SCAN_START = 0x20`, in the firmware's
   [`usb_comm.h`](https://github.com/CANShift/canshift-firmware/blob/main/src/hal/usb/usb_comm.h)).
   The tuner's CAN bus view decodes the raw stream as JSON
   `{ "can_frame": { "id": "0x370", "data": "..." } }`. Move pedals, change
   loads, watch which bytes move.

Even with vendor docs, run the scanner for 30 seconds once. Vendors miss frames
or list the wrong cadence; the bus tells the truth.

## Step 2 — Author `signals.json`

The schema is
[`SignalDefSchema`](https://github.com/CANShift/canshift-core/blob/main/src/schemas/signal.ts).
The full annotated field reference is in the
[ECU integration reference](../reference/ecu-integration.md) — do not
duplicate it here.

The default broadcast template lives at
[`data/config/signals.json`](https://github.com/CANShift/canshift-firmware/blob/main/data/config/signals.json)
in the firmware (the MaxxECU frame group). Copy it and edit.

### 2.1 Broadcast ECU

One entry per signal in the `signals` array. Required fields per signal:
`name`, `canFrameId`, `startByte`, `byteLength`, `bigEndian`, `signed`, `scale`,
`offset`, `unit`, `min`, `max`, `timeoutMs`. Optional: `warningLevel`,
`dangerLevel`, `highWarningLevel`, `highDangerLevel`, `bitMask`.
See `MAXXECU_SIGNALS` in
[`ecu-profiles/index.ts`](https://github.com/CANShift/canshift-core/blob/main/src/ecu-profiles/index.ts)
for a fully fleshed-out template, including bit-packed flag signals sharing a
status byte through `bitMask`.

Set `canSpeedKbps` at the top of the file to match the ECU's bus speed (usually
500). The schema rejects values outside the supported enum.

### 2.2 Request/response ECU (OBD-II)

OBD-II signals add a `polling` block:

```jsonc
{
  "name": "rpm",
  "canFrameId": "0x7E8",
  "startByte": 3,
  "byteLength": 2,
  "bigEndian": true,
  "signed": false,
  "scale": 0.25,
  "offset": 0.0,
  "unit": "rpm",
  "min": 0,
  "max": 8000,
  "timeoutMs": 2000,
  "polling": {
    "mode": 1,
    "pid": 12,
    "intervalMs": 250,
  },
}
```

The polling schema lives in
[`schemas/obd2.ts`](https://github.com/CANShift/canshift-core/blob/main/src/schemas/obd2.ts).
v1 constraints (enforced at parse time):

- `mode` must be `0x01` (Mode 01, current data). Other modes return a Zod error.
- `pid` is `0..0xff`. Use the standard catalog in
  [`obd2-mode01-pids.ts`](https://github.com/CANShift/canshift-core/blob/main/src/ecu-profiles/obd2-mode01-pids.ts)
  — every common PID is already listed with the J1979 scale/offset pre-decoded.
- `intervalMs` is `100..60000`. Below 100 ms a single signal saturates a
  500 kbps bus once you account for response latency; above 60 s should be a
  one-shot diagnostic instead.

Frame IDs are fixed in v1: request on `0x7DF`, response on `0x7E8`. Multi-ECU
(`0x7E9..0x7EF`) is deferred to OBD-II v2.

The `OBDII_SIGNALS` array in `ecu-profiles/index.ts` is the reference — six
standard PIDs (RPM, speed, coolant, throttle, IAT, battery) with the J1979
scale/offset formulas worked out in `obd2-mode01-pids.ts`.

### 2.3 Validate locally

```bash
cd canshift-core
npm test -- validate-signal-config
```

The validator
[`validateSignalConfig`](https://github.com/CANShift/canshift-core/blob/main/src/validation/validate-signal-config.ts)
runs the same `SignalConfigSchema.safeParse` the tuner and mobile IPC
boundaries use. It catches: malformed `canFrameId` hex, `byteLength` outside
`{1,2,4}`, threshold ordering violations, `min >= max`, bad `bitMask` hex, and
unsupported `canSpeedKbps`.

For the CAN XML import path, `parseCanXml` runs every emitted signal through the
same schema and diverts malformed rows to `warnings[]` rather than coercing
them.

## Step 3 — Bench-test against a real ECU (or a recorded log)

The tuner's CAN bus view is the most efficient verification tool.

### 3.1 Wire-up

Follow the [CAN integration notes](../reference/can-integration.md) — CAN
H/L from the ECU to the CAN Pal, dash powered, USB to the workstation. The
[first-flash checklist](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/first-flash.md) is the pre-flight if
this is the first power-up of new hardware.

### 3.2 Raw frame dump

In the tuner, open the CAN bus view. Confirm you see the frame IDs you expect at
the expected cadence. If the ECU broadcasts `0x370` at 10 Hz you should see ~10
lines per second for that ID. Frames you do not expect are either documentation
gaps or a busmate (gauge cluster, ABS, TCU, BCM) — log them, do not panic.

### 3.3 Decoded value check

Flash a config that has at least one widget per signal you authored (simplest: a
numeric label on every signal name). Verify on the dash:

- Static values match the ECU's PC software readout (RPM at idle, coolant temp
  warm).
- Sweeping values track in lockstep (throttle pedal → TPS, RPM rev).
- Sign is correct (negative coolant in the cold; pressures positive).
- No signal sits at `0` or `min`/`max` permanently — that means `scale`,
  `offset`, or `byteLength` is wrong, or you picked the wrong frame ID.

### 3.4 OBD-II specifics

After flashing, watch the scanner: a Mode 01 query frame should appear on
`0x7DF` at the configured `intervalMs`, followed within a few milliseconds by
the `0x7E8` response. If no response appears:

- Some OBD-II ECUs only answer when the engine is running (key on alone is not
  enough).
- ISO 15765-4 11-bit at 500 kbps is the assumed framing. 29-bit ID or 250 kbps
  buses need the v2 work to land first.
- Validate the PID actually exists on the ECU — PID `0x00` returns a bitmap of
  supported PIDs ("PIDs supported [01..20]").

## Step 4 — Ship the catalog entry

If the ECU is unique to your setup, keep `signals.json` in your local config and
stop here. If you want it available to other users:

1. Add a new `EcuProfile` entry to
   [`ecu-profiles/index.ts`](https://github.com/CANShift/canshift-core/blob/main/src/ecu-profiles/index.ts).
   Required fields: `id` (kebab-case, e.g. `haltech-elite-2500`), `name`,
   `description`, `protocol` (free-form version string written to
   `SignalConfig.protocol` on export), `signals: SignalDef[]`. See
   `maxxecu-street` and `obd2-j1979` for the format.
2. The new preset appears automatically in the tuner's ECU profile view (the
   view reads the registry).
3. Open a PR against `canshift-core` titled `feat: add <ecu name> preset`.
   Include in the description: the vendor doc reference, bench-test confirmation
   (per step 3), and any open-question signals you couldn't verify. Because
   consumers pull `@canshift/core` from npm, the preset reaches the tuner once
   the change is published and the tuner bumps its dependency.

The MaxxECU preset is also mirrored as the firmware's default `signals.json` so
new boards boot with a useful baseline. New presets do not need to claim the
default — the user picks in the tuner.

## OBD-II v1 — known limits

Spelled out so contributors don't write code against assumptions that won't
hold:

- **Mode 01 only.** Mode 02 (freeze frame), Mode 09 (vehicle info), and
  diagnostic services land later.
- **Single ECU.** Response frame is hard-pinned to `0x7E8`. Multi-ECU vehicles
  (`0x7E9..0x7EF`) need the dispatcher work.
- **No ISO-TP reassembly.** Multi-frame responses (long PIDs, VIN read) are
  dropped. v1 is single-frame replies only.
- **11-bit IDs.** 29-bit OBD-II (`0x18DA F1 xx`) is not parsed.
- **500 kbps.** 250 kbps OBD-II buses (some medium-duty) are not supported.
- **Minimum interval 100 ms.** The schema rejects lower values rather than
  letting the user lock the bus by mistake.

## Validation checklist

Run through this before submitting a preset PR:

- [ ] `npm test -- validate-signal-config` passes in `canshift-core`
- [ ] The CAN bus view shows the expected frame IDs at the documented cadence
- [ ] Every signal in the preset has a sane idle reading on the dash
- [ ] Sweeping each input (throttle, brake, RPM) moves the right signal and only
      that signal
- [ ] No widget sits stuck at `min` / `max` / `0` — that means a scale or offset
      bug
- [ ] `timeoutMs` is set per signal — values that update every 5 s need
      `timeoutMs: 10000`, not the default 500 ms, or they will flicker to the
      "invalid" state on the dash
- [ ] If `warningLevel`/`dangerLevel` are set, they obey the monotonic ramp
      invariant (high-side: `warningLevel <= dangerLevel`; low-side:
      `dangerLevel <= warningLevel`)
- [ ] For OBD-II: the polling block has `mode: 1`, a PID from the standard
      catalog, and `intervalMs >= 100`
- [ ] Outbound frame IDs in the `out` block (map switch, cruise) match the ECU's
      expected input IDs — these are NOT auto-discovered

## Reference implementations

- **Broadcast (MaxxECU)** —
  [`data/config/signals.json`](https://github.com/CANShift/canshift-firmware/blob/main/data/config/signals.json)
  is the firmware default; `MAXXECU_SIGNALS` in `ecu-profiles/index.ts` is the
  same data exposed to the tuner. Mirror its shape, including the bit-packed
  flag signals sharing a single status byte.
- **OBD-II (J1979 standard PIDs)** — `OBDII_SIGNALS` in `ecu-profiles/index.ts`
  and the PID catalog in `obd2-mode01-pids.ts`.
- **CAN XML importer** —
  [`parse-can-xml.ts`](https://github.com/CANShift/canshift-core/blob/main/src/can-xml/parse-can-xml.ts).
  Pure regex parser, no XML dependency, validates every emitted signal through
  `SignalDefSchema`.

## Related

- [ECU integration reference](../reference/ecu-integration.md) — full annotated field reference
- [CAN integration notes](../reference/can-integration.md) — CAN Pal wiring + bus health
- [First flash](https://github.com/CANShift/canshift-tuner/blob/main/docs/install/first-flash.md) — pre-flight checklist for new hardware
- [Config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md) — overall config JSON contract
