# canshift-firmware/rust

Spike Rust workspace for the firmware port (issues #827, #936).

## What's here

| Crate | Phase | Status |
|---|---|---|
| `ota-hmac` | 1 (host) | ✅ 16 host parity tests vs the Unity suite at `test/test_ota_hmac/`. |
| `ota-hmac` | 2 (C ABI) | ✅ `staticlib` crate-type, `extern "C"` bridge in `src/ffi.rs`, hand-written `include/ota_hmac_rs.h`, `panic_handler` for `no_std` builds. |
| `ota-hmac` | 3 (PlatformIO link) | ⏳ Pending `espup` install on the build host — see § Phase 3 below. |

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

## Phase 3 — integrating with PlatformIO

When you're ready to actually link the Rust crate into the firmware:

### 1. Install the Xtensa toolchain on the build host

```bash
cargo install espup --locked
espup install
. ~/export-esp.sh   # source the env vars; add to shell profile for persistence
```

`espup install` downloads ~500 MB (the LLVM fork Espressif maintains for Xtensa). The `xtensa-esp32-none-elf` target appears in `rustup target list --installed`.

### 2. Add a PlatformIO pre-build script

In `canshift-firmware/platformio.ini` under `[env:crowpanel_28]`, add:

```ini
extra_scripts = scripts/extra_targets.py, scripts/build_rust.py
```

Create `canshift-firmware/scripts/build_rust.py` along these lines (sketch — adjust paths after `espup` is installed):

```python
import subprocess, os
Import("env")

RUST_DIR = os.path.join(env["PROJECT_DIR"], "rust")
TARGET = "xtensa-esp32-none-elf"
LIB = os.path.join(RUST_DIR, "target", TARGET, "release", "libota_hmac.a")

def build_rust(source, target, env):
    subprocess.check_call(
        ["cargo", "+esp", "build", "--release",
         "--manifest-path", os.path.join(RUST_DIR, "ota-hmac", "Cargo.toml"),
         "--no-default-features", "--features", "ffi",
         "--target", TARGET],
        cwd=RUST_DIR,
    )

env.AddPreAction("buildprog", build_rust)
env.Append(LIBS=[File(LIB)])
env.Append(CPPPATH=[os.path.join(RUST_DIR, "ota-hmac", "include")])
```

### 3. Write the C++ bridge

`canshift-firmware/src/hal/wifi/ota_hmac_bridge.cpp` (new file) wraps the existing `OtaHmacBackend` interface around the Rust C ABI so existing callers (`OtaHmacVerifier`) don't change. Gate the bridge behind a `USE_RUST_OTA_HMAC` build flag so the C++ path stays available as the rollback.

### 4. Measure (gate criteria from architecture-critic review on #827)

After the first successful link:

| Metric | How to capture | Budget |
|---|---|---|
| Δflash | `pio run -t size` before/after | < 50 KB |
| ΔRAM (.bss + .data) | same | < 10 KB |
| Δcompile time | `time pio run` | < 30% |
| BLE task stack high-water | `uxTaskGetStackHighWaterMark()` log after 1h | no regression |
| Heap min free over 1h | `heap_caps_get_minimum_free_size()` | no regression |

Any budget breach → abort Phase 3, keep the C++ path.

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
