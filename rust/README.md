# canshift-firmware/rust

Rust workspace for the firmware's pure-logic modules. Each crate is an rlib
with a hand-written `extern "C"` header. `canshift-ffi` bundles the enabled
crates into the one staticlib the ESP32 binary links (`scripts/build_rust.py`),
one feature per `USE_RUST_<NAME>=1` build flag in `platformio.ini`; bundling is
what lets the release profile's LTO apply, and it owns the only panic handler.

## Member crates

| Crate               | Header                           |
| ------------------- | -------------------------------- |
| `signal-map`        | `include/signal_map_rs.h`        |
| `can-parser`        | `include/can_parser_rs.h`        |
| `usb-envelope`      | `include/usb_envelope_rs.h`      |
| `config-loader`     | `include/config_loader_rs.h`     |
| `format-float`      | `include/format_float_rs.h`      |
| `error-store`       | `include/error_store_rs.h`       |
| `alert-engine`      | `include/alert_engine_rs.h`      |
| `sensor-color-ramp` | `include/sensor_color_ramp_rs.h` |
| `timer-core`        | `include/timer_core_rs.h`        |
| `layout-grid`       | `include/layout_grid_rs.h`       |
| `workshop-mode`     | `include/workshop_mode_rs.h`     |
| `ota-signature`     | `include/ota_signature_rs.h`     |
| `ble-link`          | `include/ble_link_rs.h`          |
| `bus-scan`          | `include/bus_scan_rs.h`          |
| `tone-sequencer`    | `include/tone_sequencer_rs.h`    |
| `imu-core`          | `include/imu_core_rs.h`          |
| `datalog`           | `include/datalog_rs.h`           |

## Running

```bash
cd canshift-firmware/rust

# Host parity tests (std) for every crate.
cargo test

# Static analysis.
cargo clippy --all-targets --features ffi -- -D warnings
cargo fmt --check

# Verify the ffi modules + panic_handler compile under no_std.
cargo build --release -p canshift-ffi --no-default-features --features can-parser,signal-map
```

## ESP32 build

```bash
cargo install espup --locked
espup install
. ~/export-esp.sh   # add to ~/.zshrc for persistence
```

`espup install` downloads the LLVM fork Espressif maintains for Xtensa into
`~/.rustup/toolchains/esp/`. `rust-toolchain.toml` in this directory auto-selects
it. The firmware build then invokes `scripts/build_rust.py`, which runs
`cargo build --release --manifest-path canshift-ffi/Cargo.toml --no-default-features
--features <enabled crates> --target xtensa-esp32-none-elf -Z build-std=core,alloc`
once and appends `libcanshift_ffi.a` to the link.
