# Testing

Each repository owns its test harness and runs it in its own CI on every PR.
There is no shared pipeline — you run each suite inside its repository.

## Firmware C++ — Unity on native (`canshift-firmware`)

The firmware test suite uses PlatformIO's Unity integration with the `native`
env. Tests live under `test/<suite>/test_main.cpp`.

```bash
cd canshift-firmware
pio test -e native
```

Filter to a single suite:

```bash
pio test -e native -f test_can_parser
```

What's covered:

- `test_can_parser` / `test_can_parser_signed` — `CanParser::detail::decodeBytes`
- `test_signal_map` — name → SignalId lookup
- `test_signal_store` — push / read / timeout invalidation
- `test_error_store` / `test_error_store_wrap` — ring buffer push / dismiss
- `test_format_float` — `FloatFormat` parity gate (mirrors the Rust crate)
- `test_screen_profile` — design-space scaling
- `test_sensor_color_ramp` / `test_sensor_palette`
- `test_usb_envelope` — payload-slice brace walk
- `test_config_loader` — schema version + JSON parse
- `test_logger` — UART lock + envelope framing
- `test_parse_u32_strict` — config parse helper

The `test/native/shim/` directory holds host-side fakes for HAL surfaces
(StorageDriver, etc.) so tests can run without an ESP32.

## Rust crates — cargo test (`canshift-firmware`)

Each crate under `canshift-firmware/rust/` carries its own tests.

```bash
cd canshift-firmware/rust/can-parser
cargo test
```

Run every Rust suite from the workspace:

```bash
cd canshift-firmware/rust
cargo test --workspace
```

Each crate has parity tests that exercise the same fixtures as the matching
C++ Unity suite — flipping `USE_RUST_*=1` keeps observable behaviour.

## TypeScript packages

`canshift-core` uses Jest; `canshift-tuner` and `canshift-mobile` use Vitest
and Jest respectively. Run each inside its own repository:

```bash
cd canshift-core && npm test
cd canshift-tuner && npm test
```

`canshift-core` is the schema source of truth. It is published to npm as
`@canshift/core`, so a schema change lands and is released there before the
tuner or mobile app bump their dependency. Bumping a Zod schema usually breaks
at least one schema-version test in `canshift-core` and a roundtrip test in a
consumer — land the core change first, publish, then fix the consumer against
the new version.

The firmware mirrors the core schema in `config_types.h`. Its parity and
fixture suites run against a sibling `../canshift-core` checkout and skip when
none is present, so run the firmware suite with `canshift-core` cloned
alongside it to exercise them.

## CI gates

Each repository enforces its own required checks before merge:

| Repository          | Gates                                                                       |
| ------------------- | --------------------------------------------------------------------------- |
| `canshift-firmware` | clang-format · `pio test -e native` · `cargo test --workspace` · boot smoke |
| `canshift-core`     | `lint` · `test` · `build`                                                   |
| `canshift-tuner`    | `lint` · `typecheck` · `test` · `build`                                     |
| `canshift-mobile`   | `lint` · `typecheck` · `test`                                               |

All required checks must pass before merge; every repository merges via
rebase and merge.

## Adding a new test

For firmware C++: add `canshift-firmware/test/test_<name>/test_main.cpp` plus
a one-line entry in `platformio.ini`'s `[env:native]` section if the test
needs extra includes.

For Rust: add tests in `#[cfg(test)] mod tests { … }` blocks inside the
relevant `lib.rs` or `ffi.rs`. The workspace `cargo test` picks them up
automatically.

For the TypeScript packages: colocate the test with the code it covers and let
the package's runner discover it.
