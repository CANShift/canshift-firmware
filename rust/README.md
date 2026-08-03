# canshift-firmware/rust

Rust workspace for the firmware's pure-logic modules. Each crate builds as a
`staticlib` with a hand-written `extern "C"` header and is linked into the
ESP32 binary via `scripts/build_rust.py`, gated per crate by a
`USE_RUST_<NAME>=1` build flag in `platformio.ini`.

## Member crates

| Crate               | Header                             |
| ------------------- | ---------------------------------- |
| `signal-map`        | `include/signal_map_rs.h`          |
| `can-parser`        | `include/can_parser_rs.h`          |
| `usb-envelope`      | `include/usb_envelope_rs.h`        |
| `config-loader`     | `include/config_loader_rs.h`       |
| `format-float`      | `include/format_float_rs.h`        |
| `error-store`       | `include/error_store_rs.h`         |
| `alert-engine`      | `include/alert_engine_rs.h`        |
| `sensor-color-ramp` | `include/sensor_color_ramp_rs.h`   |
| `timer-core`        | `include/timer_core_rs.h`          |
| `layout-grid`       | `include/layout_grid_rs.h`         |

## Running

```bash
cd canshift-firmware/rust

# Host parity tests (std) for every crate.
cargo test

# Static analysis.
cargo clippy --all-targets --features ffi -- -D warnings
cargo fmt --check

# Verify the ffi module + panic_handler compile under no_std.
cargo build --release --no-default-features --features ffi
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
`cargo build --release --no-default-features --features ffi --target
xtensa-esp32-none-elf -Z build-std=core,alloc` for each enabled crate and appends
the resulting `.a` to the link.
