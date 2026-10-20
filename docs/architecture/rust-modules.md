# Rust modules

The pure-logic modules under `rust/` are ports of C++ helpers, linked into the
firmware through hand-written `include/*_rs.h` headers. The source carries no
comments; the contracts a reader cannot see from the code live here.

## FFI contracts

Every `extern "C"` entry point accepts null and degrades instead of
dereferencing it. Beyond that, each pointer argument is trusted for the length
passed next to it.

| Crate           | Contract the C++ caller owes                                                                                                                                      |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `alert-engine`  | `health`, `hold` and `out` point to writable mirror structs from `alert_engine_rs.h`; `CrossedLimit` output may be null                                           |
| `can-parser`    | `data` is readable for `data_len` bytes, capped at `CAN_FRAME_MAX_BYTES`; `tokens` holds `n_tokens` entries from `compile_expr_rs`; `refs` may be null when empty |
| `config-loader` | `version` is null or NUL-terminated within `MAX_VERSION_LEN`                                                                                                      |
| `error-store`   | The caller holds the ErrorStore `portMUX` across every call — Rust assumes exclusive access to `ring`, `head`, `count` and `version`                              |
| `format-float`  | `buf` is writable for `size` bytes; `spec` is null or NUL-terminated                                                                                              |
| `signal-map`    | `name` is null or NUL-terminated; bytes are read one at a time, never past the NUL or `MAX_NAME_LEN`                                                              |
| `unit-convert`  | `unit` is null or NUL-terminated; the returned symbol pointer is static                                                                                           |
| `usb-envelope`  | Both pointers are null or readable for their length; null returns null, which is stricter than the C++ original                                                   |

## Layout and value invariants

- `AlertLevel` is `repr(u8)` because the C++ enum is `uint8_t`, and
  `FwError` is `repr(C)` because C++ reads it by pointer at the same offsets.
  Both sizes are asserted at compile time in the crate.
- The structs in `alert_engine_rs.h` (`SensorHealth`, `LevelHold`,
  `CrossedLimit`, `BusSilence`) and the `Severity` enum mirror `repr(C)` /
  `repr(u8)` types field for field; a change on one side is a change on both.
  `SEVERITY_LEVEL_COUNT` and `STALE_DASH_GROUPS_MAX` are gated by
  `check_ffi_parity.py`.
- The expression evaluators in `can_parser_rs.h` return NaN when a value
  cannot be published: an unresolved reference, a parse failure, or a
  non-finite result.
- Scalars shared across the boundary (buffer caps, error codes, state values)
  are paired in `scripts/check_ffi_parity.py`, which fails the build on drift.
  `SIGNAL_COUNT` is mirrored by `test/test_signal_map`.
- `alert-engine` mirrors the C++ comparison semantics exactly: a NaN value
  reads as normal and a NaN threshold disables its check, both because `>` is
  false against NaN. The coolant caution band is `warn - 5 °C`. The low side
  of oil pressure and battery is primary, and a low-side critical
  short-circuits the high side.
- `ble-link` owns the BLE admission policy and its one constant,
  `PAIRING_DEADLINE_MS`; the C++ guard passes the link's `encrypted` and
  `authenticated` bits and the milliseconds since the connection, nothing else.
- `bus-scan` owns the bus scan's probe sequence, the bit-rate picker and the
  seen-ID table; `BusScanRs` mirrors `repr(C)` `BusScan` field for field, and
  the C++ side only executes the install and restore actions it returns.
- `tone-sequencer` owns the tone patterns, the sequencer that plays them and
  the square-wave synthesis. The sequencer plays the critical pattern for as
  long as the caller passes `critical`, refuses any other pattern while it
  does, and returns silence otherwise; the C++ side only writes the samples it
  fills to I2S. `ToneSequencerRs`, `ToneOutputRs` and `ToneSynthRs` mirror the
  `repr(C)` structs field for field.
- `imu-core` owns the g-meter's arithmetic: the chip-to-screen axis remap, the
  level calibration into the vehicle frame, the low-pass, the eight-sector peak
  envelope and the plot geometry. `ImuCoreState` and `ImuLevelCapture` mirror
  `repr(C)` `ImuCore` and `LevelCapture`; the C++ side owns the I2C bus and the
  task, and never touches the arithmetic. It carries its own Newton square root,
  since `f32::sqrt` is not in `core`.
- `format-float` exists to keep newlib's `%f`/`%g` out of the image, which
  costs about 30 KB of flash. It returns `snprintf`-style lengths, truncates
  rather than rounds, and prints NaN and infinity as literals. `format_general`
  emits valid JSON numbers, since the tuner parses telemetry as JSON.
- `usb-envelope` searches length-bounded rather than with `strstr`, so an
  embedded NUL cannot cut the scan short.
- `config-loader` is stricter than `strtol`: it rejects leading whitespace and
  signs. Version strings in production never hit the difference.

## Toolchain

`rust/rust-toolchain.toml` pins the Espressif `esp` fork because
`xtensa-esp32-none-elf` is not an upstream LLVM target; run `espup install`
first. CI's host pass overrides it with `RUSTUP_TOOLCHAIN=stable`.

`rust/.cargo/config.toml` sets `build-std-features = []` in the file rather
than on the command line, where `-Z build-std-features=` parses as `[""]`. The
default, `compiler-builtins-mem`, exports unmangled `memcpy`/`memmove`/`memset`
that collide with ESP-IDF's IRAM copies and crash the boot in `read_id_core`
before the flash cache is up. `build-std` itself is passed only by
`scripts/build_rust.py` when targeting Xtensa, so host `cargo test` does not
rebuild `core`.

Each crate builds with `std` by default so `cargo test` needs no flags; the
firmware build uses `--no-default-features --features ffi` through the
`canshift-ffi` bundle.
