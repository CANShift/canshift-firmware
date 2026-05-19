# canshift-firmware/rust

Spike Rust workspace for the firmware port (issues #827, #936).

## What's here

| Crate | Status | Purpose |
|---|---|---|
| `ota-hmac` | Phase 1 — host parity only | Rust port of `src/hal/wifi/ota_hmac.cpp` — rolling-window HMAC-SHA256 trailer verifier for OTA uploads. 16 tests verifying byte-for-byte parity with the Unity suite + the real RustCrypto HMAC-SHA256 path. |

## Phase 1 scope (THIS PR)

- Workspace skeleton, `cargo build` + `cargo test` work on the host (x86_64).
- `ota-hmac` crate ports the streaming logic; no `unsafe`, no `alloc` in the
  hot path, fully `no_std` compatible (`#![cfg_attr(not(test), no_std)]`).
- Parity tests mirror the Unity suite at
  `canshift-firmware/test/test_ota_hmac/test_main.cpp` 1-for-1.
- Production path uses `hmac` + `sha2` (RustCrypto) — the same algorithm
  mbedTLS implements on the C++ side.

## Phase 2 scope (next PR if Phase 1 review goes well)

- Add `crate-type = ["staticlib"]` to `ota-hmac`.
- Espressif Xtensa toolchain via `espup install` (~500 MB, target
  `xtensa-esp32-none-elf`).
- PlatformIO pre-build script invoking `cargo build --release --target xtensa-esp32-none-elf`
  and adding the resulting `.a` to the linker inputs for `env:crowpanel_28`.
- C ABI bridge — `extern "C"` wrapper functions with `#[repr(C)]` types so
  `ota_hmac.cpp` can call into Rust via a stable header.
- `#[panic_handler]` strategy: `panic-halt` for release builds, log + reset
  via the existing diag/logger in debug.
- Measurement gates from the architecture-critic review (issue #827
  comment): Δflash, ΔRAM, Δcompile time, task stack high-water.

## Running

```bash
cd canshift-firmware/rust
cargo test          # 16/16 pass on host
cargo clippy --all-targets -- -D warnings
cargo fmt --check
```

## Why this lives at `canshift-firmware/rust/`

The firmware is the consumer. PlatformIO needs the Cargo workspace at a
predictable path so the pre-build script (Phase 2) can locate it via
`$PROJECT_DIR/rust/`. Mirrors the `canshift-firmware/test/` layout for
Unity host tests.
