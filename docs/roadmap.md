# Roadmap

> 🚨 **Pre-#1351 snapshot.** The roadmap below was written before the WiFi+SPA removal and the `canshift-tuner` introduction ([`#1351`](https://github.com/CANShift/issues/1351)). Items referencing `canshift-studio-web` / dash-hosted Studio / WiFi are superseded by the tuner direction. The forward-looking sections still track the right product themes; the surface naming is stale.

A living snapshot of where the project is and what's next. Each section
points back at the GitHub issues that drive the actual work — when this
file and the issue tracker disagree, the tracker wins.

Last refreshed: 2026-05-18 (see [#849](https://github.com/CANShift/issues/849)).

---

## Done — shipped today

Everything below is live on `main` and exercised by the CI gates.

### Firmware (canshift-firmware @ PlatformIO / ESP32)

- LVGL 8.3 dashboard, lazy page build, day/night theming.
- Widgets: gauge (arc + bar + numeric), bar, button, gear, image, label,
  timer, warning. Each editable from Studio.
- CAN signal pipeline — `signals.json` schema-driven decode of any
  passive-broadcast ECU. MaxxECU 0x370-0x375 group ships as the example
  default; see [`docs/ecu-integration.md`](reference/ecu-integration.md).
- Settings panel — drag-down gesture from the top edge, persisted
  brightness / sleep / rotation / day-mode toggles.
- Error bar — bottom overlay with swipe-up to expand, scrollable error
  list ([#642](https://github.com/CANShift/issues/642)).
- Diag drawer — ECU flag bits, status grid, firmware error log
  ([#635](https://github.com/CANShift/issues/635)).
- Physical GPIO buttons — wire any input pin to a dashboard action (page
  nav, map switch, CAN raw, cruise op)
  ([#833](https://github.com/CANShift/issues/833)).
- Boot sequence — splash + progress bar, atomic SPIFFS writes with `.bak`
  fallback, embedded default config provisioning on first boot.
- USB JSON-line protocol — Studio config push / pull / scan.
- BLE GATT — TELE notify, STATUS read+notify, SETTINGS read+write, CMD.
- Wi-Fi softAP for future OTA — gated by `APP_WIFI_OTA_ENABLED`.
- OTA HMAC bearer (signed firmware artifacts) — secrets in `secrets.ini`,
  static mbedTLS context to avoid heap fragmentation in OTA path.
- Defensive runtime — task watchdog on UI/CAN/USB, perf counters, alert
  engine (rev limiter flash, warning thresholds).

### Studio (canshift-studio-web @ dash-hosted React SPA, #1077)

Originally shipped as `canshift-studio` (Electron + React); decommissioned
post-cutover in favour of the dash-hosted Vite + React SPA served from the
firmware over WiFi AP. Feature inventory below covers both incarnations —
the dash-hosted Studio inherited the editor surface from the Electron app.

- Visual dashboard editor — drag-drop canvas, widget selection, property
  panel split per widget type
  ([#697](https://github.com/CANShift/issues/697)).
- Signal config route — preset picker (Generic, MaxxECU, OBD-II)
  ([#19](https://github.com/CANShift/issues/19)),
  per-signal editor, CAN XML import.
- Device config route — CAN speed, TWAI pin picker with chip-safe ∩
  board-available validation
  ([#831](https://github.com/CANShift/issues/831)).
- Physical button bindings UI inside Device Config
  ([#833](https://github.com/CANShift/issues/833)).
- USB connect / push / scan flow, firmware version query, day-night
  toggle, touch calibration trigger.
- Firmware update route — GitHub release picker, download + flash via
  esptool, manual local-bin flash.
- Friendly Zod error messages — IPC validation summaries surface as
  user-actionable strings
  ([#832](https://github.com/CANShift/issues/832)).
- CLI panel (logs / commands) with detachable window.

### Core (canshift-core @ TypeScript)

- Strict Zod schemas as the single source of truth for dashboard, signal,
  device, input-bindings, track-telemetry. Wire (snake_case) ↔ domain
  (camelCase) mappers at file/IPC boundaries.
- Migration framework — 1.0.0 → 1.14.0 chain registered, automatic
  upgrade on config import.
- Design tokens (dark + placeholder light), hardware profile table for
  per-board pin reserved sets, ECU preset registry.

### Mobile (canshift-mobile @ Expo / React Native)

- Live telemetry view over BLE.
- Settings / device pairing screens.
- USB / BLE transport abstractions.
- Wired up to canshift-core schemas — same Zod contracts as Studio.

---

## In flight — open umbrellas

Issues actively being decomposed into sub-issues; expect movement on
these across the next few sprints.

| Umbrella                                                                          | Status                                                                                                                                                                                                                                                                                                         |
| --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [#556](https://github.com/CANShift/issues/556) — ECU-agnostic firmware            | Phase 1 (rename, [#840](https://github.com/CANShift/issues/840)) ✓ · Phase 2 (preset library, [#19](https://github.com/CANShift/issues/19)) ✓ · Phase 3 (OBD-II polling, [#841](https://github.com/CANShift/issues/841)) — open · Phase 4 (integration docs, [#842](https://github.com/CANShift/issues/842)) ✓ |
| [#815](https://github.com/CANShift/issues/815) — Track mode                       | Core schema ([#843](https://github.com/CANShift/issues/843)) ✓ · Firmware indicator ([#844](https://github.com/CANShift/issues/844)) — open · Mobile timing engine ([#845](https://github.com/CANShift/issues/845)) — blocked · History screens ([#846](https://github.com/CANShift/issues/846)) — blocked     |
| [#158](https://github.com/CANShift/issues/158) — Firmware refactor & optimization | Long-running tracking issue; PRs land continuously.                                                                                                                                                                                                                                                            |

---

## Next — concrete near-term issues

Tracked by individual issues, ranked by current priority:

- [#451](https://github.com/CANShift/issues/451) — optional
  cruise control screen with +/−/SET/OFF buttons. Schema + GPIO wiring
  already ships via [#833](https://github.com/CANShift/issues/833)
  - [#852](https://github.com/CANShift/issues/852) — this PR
    adds the on-screen consumer.
- [#21](https://github.com/CANShift/issues/21) — visual
  theme editor: live palette + widget-style preview, exported to device.
- [#548](https://github.com/CANShift/issues/548) — per-board
  target screen profile selector with profile-specific dashboard layouts.
- [#531](https://github.com/CANShift/issues/531) — ESP32
  secure boot v2 + flash encryption. Audit-ready flash image for the
  production env.
- [#436](https://github.com/CANShift/issues/436) — Expo SDK
  52 → 53 upgrade. Unblocks the remaining 4 `tar` Dependabot alerts and
  the rest of the mobile feature backlog.
- [#191](https://github.com/CANShift/issues/191) — optional
  bespoke per-widget text colours in day mode.

---

## Speculative — no tracking issue yet

Ideas worth keeping in mind but explicitly not scheduled. If any of these
shifts to "actively scoped", open a GitHub issue and lift it into the
**Next** section.

- Data logging (SPIFFS ring or SD card add-on) once persistent storage
  budget is reviewed.
- Shift light LEDs over external GPIO — natural extension of the GPIO
  button work in [#833](https://github.com/CANShift/issues/833).
- Predictive shift indicator from rolling RPM derivative.
- Engine knock display when the ECU exposes a knock count on CAN.
- Multi-config profiles switchable from the dash itself (not just
  Studio).

---

## How to use this document

- If you're a contributor wondering what's safe to pick up, scan **Next**
  first — those issues have tracker visibility and any blockers are
  surfaced.
- If you're a user wondering whether feature X exists, **Done** is
  authoritative. If it's not listed there it's either in flight or not
  scheduled.
- Anything that looks stale here is a documentation bug — file under
  `docs:` and either fix it directly or assign to the next person
  touching docs.
