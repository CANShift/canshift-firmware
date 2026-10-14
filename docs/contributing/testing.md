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

## Regression bench (`canshift-firmware`)

The bench catches regressions that pass every unit test: an image that grows,
a hot path that slows down, a behaviour or a frame that changes. It needs no
hardware and runs in `build.yml`; every gating job is in the `needs` of
`ci-success`.

### Size budgets

`bench/size-budget.json` holds, per board, the bytes each build may take:

| Metric  | Measured from                                                  |
| ------- | -------------------------------------------------------------- |
| `flash` | `firmware.bin`, the image written to the app partition         |
| `dram`  | `.dram0.data` + `.dram0.bss` + `.noinit` — static internal RAM |
| `iram`  | every `.iram0.*` section                                       |
| `psram` | every `.ext_ram*` section — static PSRAM, `0` on every board   |

Each `firmware` matrix leg measures the ELF it just linked
(`scripts/bench_size.py measure`) and uploads `size-<board>.json`; the `size budget`
job compares the five reports against the budget and writes the delta
table to the job summary. A board fails when it grows past its budget plus the
per-metric `tolerance` in the same file, or when the budget and the build
matrix disagree on the board list.

Raising a budget is a decision, not a fix: change the numbers in
`bench/size-budget.json` in the PR that grows the image, and say why in its
description. Lower them when an image shrinks, so the next growth is measured
from where it really is. To reproduce the check locally after a build:

```bash
mkdir -p sizes
python3 scripts/bench_size.py measure --board crowpanel_28 \
  --elf .pio/build/crowpanel_28/firmware.elf \
  --bin .pio/build/crowpanel_28/firmware.bin --out sizes/size-crowpanel_28.json
python3 scripts/bench_size.py check --measured sizes
```

The check needs a report for every board, so measure all five first. Local
numbers drift from CI's by a few bytes (paths baked into assert strings), which
the tolerance absorbs; take budget numbers from CI instead. Download the
`size-*` artifacts of the PR's run and write them into the budget:

```bash
gh run download <run-id> --pattern 'size-*' --dir sizes
python3 scripts/bench_size.py update --measured sizes
```

### Performance bench

`pio run -e bench` builds `.pio/build/bench/program`, a host binary that links
the firmware's own CAN parser, expression VM, telemetry encoder, status writer,
float formatter and the complete USB/WebSocket command dispatcher at `-O2`,
with fakes only for the hardware (`bench/host/`, `bench/host/shims/`). Its
`perf` mode times each hot path:

| Case               | What one operation is                                                                                                                               |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `calibration`      | a fixed integer loop, the yardstick for the runner's speed                                                                                          |
| `can_decode`       | `CanParser::parseFrame` over the bundled `signals.json` frames                                                                                      |
| `expr_eval`        | `eval_tokens_rs` over five representative compiled expressions                                                                                      |
| `telemetry_encode` | `TelemetryFrame::encode` of a 14-field frame                                                                                                        |
| `status_writer`    | `UsbStatusLine::format`                                                                                                                             |
| `format_float`     | `FloatFormat::formatFixed` across values and precisions                                                                                             |
| `command/<class>`  | `handleCommand` for status, ping, a shared command, a config read, an OTA rejection, an unknown command and a parse error, over the WebSocket route |

Each case calibrates its batch to about 2 ms, warms up, then takes 31 batches
and reports the median and quartiles in ns per operation.

`scripts/bench_perf.py` runs the head binary and the baseline binary five
times each, alternating, and compares the median of the five medians. A case
fails only when it is more than 10 % slower **and** every head round is slower
than every baseline round; a slower median whose rounds overlap is reported as
`noisy` and does not fail. A suspected regression gets five more rounds of each
side before the verdict. The `noise` column is the largest run-to-run spread
(max − min over median) of either side.

In CI the `perf bench` job checks out the PR's base commit next to the head,
builds the bench in both, and compares them on the same runner, so runner
generations never mix. When the base predates the bench env it compares the
head with itself. To run it locally:

```bash
pio run -e bench
python3 scripts/bench_perf.py                       # head against itself: the noise floor
python3 scripts/bench_perf.py --base-root ../main   # a second checkout with its own bench build
```

Measured on `ubuntu-latest` over five A/A runs, the largest median delta was
4.4 % (`command/ota_reject`), every other case stayed under 2 %, and single
rounds spread up to 16 %: the median-of-medians and the separation rule absorb
that without a false failure, while an injected 20 % slowdown is caught.

A deliberate slowdown, such as a new feature in a hot path, still fails the
job; say so in the PR and let the reviewer accept it.

### Behaviour goldens

The same `bench` binary replays recorded behaviour and `scripts/bench_goldens.py`
compares it, byte for byte, with the files committed under `bench/goldens/`.
Time is virtual in both modes (`sim/shims/sim_clock.h`), so log timestamps,
timeouts and poll schedules are reproducible.

| Golden             | Input                                       | Output                                                    |
| ------------------ | ------------------------------------------- | --------------------------------------------------------- |
| `can/maxxecu_demo` | `trace.txt` over the bundled `signals.json` | `timeline.golden`: every frame and every signal change    |
| `can/obd2_j1979`   | `trace.txt` plus the case's `signals.json`  | the same, including the OBD-II requests the poller sends  |
| `protocol/<name>`  | `<name>.in`, one command line each          | `<name>.golden`: the reply lines, over USB then WebSocket |

A CAN trace line is `<ms> rx <id> <hex payload>` or `<ms> end`; frames go
through `CanRxRoute::deliver`, the same route the TWAI task uses, while the
OBD-II poller and signal timeouts tick every 10 ms of virtual time. A protocol
golden runs each `.in` file twice, through `dispatchFromSerial` and
`dispatchFromSocket`, on a fresh copy of `data/`; `>` is the command, `<` a line
the transport received (log lines included, as a host sees them) and
`~ restart` a restart the command requested. The protocol inputs cover status,
ping, the shared commands, chunked config put and get, typed config get and put,
and OTA begin/write/end with `ota_unsigned`, `ota_bad_signature`,
`sha256_mismatch`, `not_app_image` and the offset errors.

The traces and `.in` files are generated: edit `scripts/bench_inputs.py`, then
run `python3 scripts/bench_inputs.py`. Goldens change only on purpose:

```bash
pio run -e bench
python3 scripts/bench_goldens.py                    # what CI runs
python3 scripts/bench_goldens.py --update-goldens   # rewrite them, then review the diff
```

A golden records today's behaviour, bugs included: the PR that fixes one
updates its golden in the same change.

CI never passes `--update-goldens`. On a mismatch the job prints a unified diff
and uploads the actual output as the `goldens-report` artifact. A golden that
changes in a PR is a behaviour change: the diff is part of the review.

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

| Repository          | Gates                                                                                                                                                           |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `canshift-firmware` | clang-format and parity scripts · `cargo test`/`clippy`/`fmt` · `pio test -e native` · per-board build with flash budget and SPIFFS round-trip · `secure` build |
| `canshift-core`     | `lint` · `test` · `build`                                                                                                                                       |
| `canshift-tuner`    | `lint` · `typecheck` · `test` · `build`                                                                                                                         |
| `canshift-mobile`   | `lint` · `typecheck` · `test`                                                                                                                                   |

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
