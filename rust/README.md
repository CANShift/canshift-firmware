# canshift-firmware/rust

Spike Rust workspace for the firmware port (issues #827, #936).

## What's here

| Crate | Phase | Status |
|---|---|---|
| `ota-hmac` | 1 (host) | ✅ 16 host parity tests vs the Unity suite at `test/test_ota_hmac/`. |
| `ota-hmac` | 2 (C ABI) | ✅ `staticlib` crate-type, `extern "C"` bridge in `src/ffi.rs`, hand-written `include/ota_hmac_rs.h`, `panic_handler` for `no_std` builds. |
| `ota-hmac` | 3 (PlatformIO link) | ✅ `env:crowpanel_28_rust` links the Xtensa staticlib via `scripts/build_rust.py`. Δflash = **−444 B** vs the mbedTLS path. |

## Phase 2 — what just landed

- `crate-type = ["rlib", "staticlib"]` — `cargo build --release --no-default-features --features ffi` emits `target/release/libota_hmac.a` ready to link into the ESP32 binary.
- `src/ffi.rs` — 6 `extern "C"` entry points + opaque `OtaHmacRs` state struct, `#[repr(C)]`. Zero `alloc`: the verifier sits in caller-owned storage sized via `ota_hmac_rs_sizeof()` / `ota_hmac_rs_alignof()`. The C++ bridge will reserve the buffer on its stack.
- `include/ota_hmac_rs.h` — hand-written header (~50 lines). Versioned beside the Rust source so any signature change moves both in lockstep.
- `panic_handler` cfg-gated behind `feature = "ffi"` AND `not(test)` AND `not(feature = "std")` — host tests use `std`'s native unwinder; only the `staticlib` build pulls the halt-forever handler.

## Running

```bash
cd canshift-firmware/rust

# Host parity tests — uses std, 16 cases.
cargo test

# Static analysis.
cargo clippy --all-targets --features ffi -- -D warnings
cargo fmt --check

# Phase 2 staticlib build (still host x86_64, just verifies the ffi
# module + panic_handler compile under no_std).
cargo build --release --no-default-features --features ffi
```

## Phase 3 — PlatformIO integration

### Prerequisites on the build host

```bash
cargo install espup --locked
espup install
. ~/export-esp.sh   # add to ~/.zshrc for persistence
```

`espup install` downloads ~500 MB (the LLVM fork Espressif maintains for Xtensa) into `~/.rustup/toolchains/esp/`. `rust-toolchain.toml` in this directory then auto-selects it, so `cargo build` Just Works.

### Building the firmware with the Rust path

```bash
pio run -e crowpanel_28_rust
```

The `env:crowpanel_28_rust` PlatformIO env defined in `platformio.ini`:
1. Sets `-DUSE_RUST_OTA_HMAC=1` — compiles `ota_hmac_bridge.cpp`, drops the mbedTLS backend in `ota_hmac.cpp` (linker conflict prevention).
2. Adds `scripts/build_rust.py` to `extra_scripts` — that hook invokes `cargo build --release --target xtensa-esp32-none-elf -Z build-std=core,alloc` and appends the resulting `.a` + the C header dir to the link.

### Measured results (this host, Apple Silicon)

| Metric | Budget | Measured | Verdict |
|---|---|---|---|
| Δflash | < 50 KB | **−444 B** (Rust path is smaller) | ✅ |
| ΔRAM (.bss + .data) | < 10 KB | 0 (identical) | ✅ |
| Δcompile time | < 30% | +20% (~8 s for the Rust path) | ✅ |

ΔRAM = 0 is expected — both paths use a single static HMAC context. The flash gain comes from LTO being able to dead-code-strip more aggressively over the focused RustCrypto `Hmac<Sha256>` than over the generic mbedTLS `mbedtls_md_*` indirection layer.

### Still TODO before flipping the default

- Flash to device + run an OTA upload through the new backend (mobile app sends `start_wifi_ap` → studio uploads a signed firmware). Verify the HMAC trailer is accepted.
- BLE task stack high-water + heap min-free over 1h — Rust monomorphisation typically adds frames; the architecture-critic flagged this as a risk.
- Once both checks pass, swap the default in `platformio.ini` (`USE_RUST_OTA_HMAC=1` in `env:crowpanel_28`) and consider this Phase 3 truly closed.

## Layout

```
canshift-firmware/rust/
├── Cargo.toml                  workspace
├── README.md                   this file
└── ota-hmac/
    ├── Cargo.toml              crate manifest
    ├── include/ota_hmac_rs.h   C ABI header (Phase 2)
    ├── src/
    │   ├── lib.rs              safe Rust API + panic_handler
    │   └── ffi.rs              extern "C" bridge (Phase 2)
    └── tests/
        ├── parity.rs                   13 cases mirror Unity stub backend
        └── production_backend.rs       3 cases vs real HMAC-SHA256
```
