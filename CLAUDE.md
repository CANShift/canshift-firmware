# canshift-firmware — Project Rules

ESP32 firmware for the CANShift dashboard (org: github.com/CANShift). C++17 · Arduino · PlatformIO · LVGL 8.3, with Rust modules under `rust/` (FFI via cbindgen).

## Commands

- `pio run -e debug` — dev build (prod env requires the OTA HMAC secret)
- `pio test -e native` — native tests; `pio run -e crowpanel_28 -t buildfs` — SPIFFS
- Stale `.pio/build` can corrupt — `rm -rf .pio` fixes it. RTK truncates pio logs: use `rtk proxy` + exit codes, not `tee | tail`.

## Rules

- New pure-logic modules in Rust; HAL/LVGL/Arduino stay C++.
- `src/config/config_types.h` mirrors `@canshift/core` schemas; `core-schema-version.txt` pins `CURRENT_SCHEMA_VERSION` for standalone builds (a sibling `../canshift-core` checkout wins) — update the pin on every schema bump.
- Firmware visuals are the canonical rendering of the brand (assets/design refs in CANShift/canshift-brand); the tuner preview mirrors them via core `widget-metrics.ts`.
- Zero comments policy (rationale lives in canshift-docs); clang-format gates every PR.

## Workflow

- Branch `type/short-description`; Conventional Commits, subject only.
- PR via `gh pr create`; required checks `lint` (clang-format), `build + tests`; **rebase and merge only**.
- Release: bump `package.json` version, merge — the workflow tags, builds and drafts the release with merged/firmware/spiffs artifacts. Write substantive release notes before publishing (the docs changelog renders them).
