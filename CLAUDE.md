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

## Code shape

Non-negotiable. Reviewed on every PR, ahead of feature count.

- Guard clauses first. Nesting depth 2 max — a third level means extract a named function.
- ~30 lines per function, ~300 per file. `create()`, `update()` and `tick()` are the usual offenders: one responsibility each, split before adding.
- Every return code is consumed at the call site. Never call a `bool` or `esp_err_t` function as a bare statement; never fail silently — `LOG_ERROR` plus `ErrorStore::push`. Persistent writes (NVS, storage) are checked before anything reports success.
- Acquisition is RAII. A take/lock in one function whose release lives in another is a bug, and a helper must be named for what it actually does.
- Dispatch is a `constexpr` table. No `switch` with inline case bodies, no `strcmp` chain, and never two transports carrying separate copies of one command set.
- `constexpr` in a namespace, not `#define`. `#define` is for preprocessor conditions and build flags only. Constants shared with Rust are generated, not hand-mirrored.
- Third copy gets extracted — LVGL style ladders and show/hide toggles included. If a helper already exists in one file, promote it to `WidgetHelpers` instead of retyping it.
- A constant must be read by the code it names. `(void)symbol;` to silence an unused warning means delete the symbol.
- The zero-comment rule covers `rust/` too.

## Workflow

- Branch `type/short-description`; Conventional Commits, subject only.
- PR via `gh pr create`; required checks `lint` (clang-format), `ci-success` (aggregates `native tests` + the per-board `firmware` build matrix + `secure`); **rebase and merge only**.
- Release: bump `package.json` version, merge — the workflow tags, builds and drafts the release with merged/firmware/spiffs artifacts. Write substantive release notes before publishing (the docs changelog renders them).
