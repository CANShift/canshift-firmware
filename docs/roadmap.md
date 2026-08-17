# Roadmap

What exists today, and where to find what is planned.

The previous version of this page ranked upcoming work by priority and went
stale within months — it was still describing a WiFi-served configurator and a
migration chain that stopped at 1.14. A hand-maintained ranking alongside four
issue trackers is a second source of truth that always loses, so this page no
longer keeps one. **The trackers are the roadmap:**

| Repo     | Issues                                                                                    |
| -------- | ----------------------------------------------------------------------------------------- |
| Firmware | [CANShift/canshift-firmware/issues](https://github.com/CANShift/canshift-firmware/issues) |
| Core     | [CANShift/canshift-core/issues](https://github.com/CANShift/canshift-core/issues)         |
| Tuner    | [CANShift/canshift-tuner/issues](https://github.com/CANShift/canshift-tuner/issues)       |
| Mobile   | [CANShift/canshift-mobile/issues](https://github.com/CANShift/canshift-mobile/issues)     |

What stays here is the part a tracker answers badly: whether a given capability
exists at all, right now.

## What ships today

### Firmware

- LVGL 8.3 dashboard — up to 8 pages, 12 widgets each, lazy page build, swipe
  navigation, day and night theme faces.
- Seven widget types: gauge, warning, button, timer, gear, image, shift light.
  Every one is editable from the tuner.
- Schema-driven CAN decode — `signals.json` describes any passive-broadcast
  ECU. A MaxxECU-style catalogue ships as the default; see
  [ECU integration](reference/ecu-integration.md).
- OBD-II polling for request/response ECUs, mode 01.
- Five board profiles, selected at compile time, with UI authored against a
  320×240 design space and scaled to the panel.
- Settings drawer — brightness, sleep, rotation, BLE toggle, touch calibration,
  persisted in NVS.
- Error bar and diagnostics drawer — ECU flag bits, status grid, error ring.
- Physical GPIO buttons — any input pin bound to a dashboard action.
- Cruise control page template.
- USB CDC transport — config push and pull, CAN scan, OBD DTC read and clear,
  firmware OTA, all on one command table.
- BLE GATT — binary telemetry notify, status, settings, commands.
- Secure boot v2 and flash encryption on the `secure` env, with signed release
  artifacts.
- Ten Rust crates compiled into every build — parsers, formatters, state
  machines. See [Build flags](reference/build-flags.md).
- Native SDL simulator, so the UI can be exercised without hardware.

### Core

- Strict Zod schemas for dashboard, signals, device, input bindings, ECU
  profiles, and the BLE and USB wire frames.
- A migration chain from 1.0.0 to the current schema version, with a validator
  that proves the chain has no gap. See
  [Config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md).
- Design tokens, theme presets, widget metrics mirrored by the firmware
  renderer, and the lap-detection engine shared with mobile.
- Published to npm as `@canshift/core`.

### Tuner

- Dashboard editor — drag-to-bind from a live CAN scan, undo/redo, autosave,
  multi-project switching, `.canshift` import and export.
- Signal editor with the built-in ECU catalogue and CAN XML import.
- Device config — CAN speed and TWAI pins with chip-safe validation, plus the
  physical button bindings.
- Live data, logs, and a CLI panel.
- In-browser flasher over Web Serial — no separate app to install.
- Opt-in analytics, off until the user turns it on.

### Mobile

- Live BLE telemetry, pairing and automatic reconnect.
- Lap detection from the shared core engine.

Deferred: the mobile app is verified in simulator only until hardware time
frees up.

## Multi-repo work

Two umbrellas span repositories and are the place to look before starting
anything large:

- **Track mode** — GPS lap timing, recorded circuits, on-firmware indicator:
  [core#2](https://github.com/CANShift/canshift-core/issues/2) and
  [mobile#12](https://github.com/CANShift/canshift-mobile/issues/12).
- **Dash design deviations** — the binding spec versus what the firmware
  currently renders: [firmware#127](https://github.com/CANShift/canshift-firmware/issues/127).

A change often spans repos: a new widget needs its schema in core (published to
npm), its renderer in the firmware, and its editor surface in the tuner. See
[Add a dashboard widget](contributing/add-widget.md).
