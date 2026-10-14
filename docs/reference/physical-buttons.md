# Physical buttons

Source: [`src/runtime/input_buttons.cpp`](../../src/runtime/input_buttons.cpp)

The dash can be driven by GPIO buttons as well as by touch. Each binding watches
one pin, recognises one kind of press, and fires the same action a dashboard
button widget would.

## The file

Bindings live in `/config/input_bindings.json` on SPIFFS. The file is optional:
without it, or with an empty list, no input task starts at all.

```json
{
  "input_bindings": [
    {
      "id": "btn_lap",
      "pin": 34,
      "active": "low",
      "pullup": false,
      "debounce_ms": 25,
      "kind": "short",
      "action": { "category": "device", "type": "timer_control", "op": "lap" }
    },
    {
      "id": "btn_track_page",
      "pin": 35,
      "active": "low",
      "pullup": false,
      "debounce_ms": 25,
      "kind": "long",
      "action": { "category": "dashboard", "type": "navigate", "pageId": "track" }
    }
  ]
}
```

The schema is `input-bindings.ts` in `@canshift/core`
([config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md)),
and it travels over USB with `CMD_GET_INPUT_BINDINGS` / `CMD_PUT_INPUT_BINDINGS`
([USB transport](../architecture/usb-transport.md)). A PUT reboots the dash to apply.

| Field         | Default   | Meaning                                                                                                                                  |
| ------------- | --------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `id`          | —         | Name shown in the logs                                                                                                                   |
| `pin`         | —         | GPIO number                                                                                                                              |
| `active`      | `"low"`   | `"low"`: button to GND, pressed reads LOW. `"high"`: button to 3.3 V with an external pull-down                                          |
| `pullup`      | `true`    | Enables the internal pull-up; only applied with `active: "low"`                                                                          |
| `debounce_ms` | `20`      | Software debounce, clamped to 1–500 ms                                                                                                   |
| `kind`        | `"short"` | `"short"`, `"long"` (held 600 ms) or `"double"` (two presses at most 250 ms apart)                                                       |
| `action`      | —         | Same shape as a button widget's action (core `button-action.ts`): `navigate`, `map_switch`, `can_raw`, `cruise_control`, `timer_control` |
| `signal`      | none      | Signal to toggle between 0 and 1 on each press, shared with on-screen buttons bound to it                                                |

`cruise_control` is accepted but only logged: the dash does not drive cruise control yet.

A short press fires only once the double-press window has passed, so a binding
whose `kind` is `"short"` reacts about 250 ms after release.

## What the firmware rejects

The loader in [`config_file_loaders.inc`](../../src/config/config_file_loaders.inc)
drops a binding, with a warning in the log, when:

- `pin` is outside 0–39,
- `pin` is one of the board's TWAI pins,
- `action` is missing or its type is unknown.

At most 16 bindings load (`CFG_MAX_INPUT_BINDINGS`); extras are dropped. The finer
pin allowlist, which refuses the flash pins 6–11, is enforced by core's schema
before the tuner burns the file.

## Behaviour at run time

A dedicated `input` task (core 0, priority 7, 2 KB stack) polls every bound pin
each millisecond, debounces, classifies the press, then hands the action to
`ActionDispatcher`, the same path touch buttons take.

While a critical alert takeover is on screen, any binding acknowledges the alert
instead of firing its action.

## Wiring

The usual pattern is a button between the GPIO and GND with `active: "low"` and
the internal pull-up on. Released reads HIGH, pressed reads LOW.

GPIO 34–39 on the ESP32 are input-only and have no internal pull-up. Fit an
external 10 kΩ pull-up to 3.3 V and set `pullup: false`, as in the example above.
