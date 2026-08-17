# Dev setup

CANShift is no longer a monorepo. Each project lives in its own repository
under the [CANShift organization](https://github.com/CANShift), with its own
CI, release flow, and `CLAUDE.md`. Work inside the repository you are
changing.

## The repositories

| Repository                                                           | Stack                                   | Role                                                   |
| -------------------------------------------------------------------- | --------------------------------------- | ------------------------------------------------------ |
| [`canshift-firmware`](https://github.com/CANShift/canshift-firmware) | C++17 / PlatformIO / LVGL, Rust modules | ESP32 dashboard firmware — releases ship from here     |
| [`canshift-core`](https://github.com/CANShift/canshift-core)         | TypeScript / Zod                        | Shared contracts, published to npm as `@canshift/core` |
| [`canshift-tuner`](https://github.com/CANShift/canshift-tuner)       | Vite + React                            | Browser configurator + flasher, deployed on Vercel     |
| [`canshift-mobile`](https://github.com/CANShift/canshift-mobile)     | Expo / React Native                     | iPhone companion app — currently deferred              |
| [`canshift-docs`](https://github.com/CANShift/canshift-docs)         | Astro + Starlight                       | This site                                              |

The retired monorepo is archived at
[`CANShift/CANShift`](https://github.com/CANShift/CANShift) for history only —
do not open new work against it.

## Which repository to contribute to

| You want to change…                                    | Repository          |
| ------------------------------------------------------ | ------------------- |
| On-device UI, LVGL widgets, CAN decoding, boot, HAL    | `canshift-firmware` |
| Config/signal schemas, migrations, ECU presets, tokens | `canshift-core`     |
| Editor UI, live data, flasher, CLI, themes             | `canshift-tuner`    |
| BLE telemetry or settings on the phone app             | `canshift-mobile`   |
| These docs                                             | `canshift-docs`     |

A single change often spans repositories — a new widget needs its schema in
`canshift-core` (published to npm), its renderer in `canshift-firmware`, and
its editor surface in `canshift-tuner`. See
[Add a dashboard widget](../contributing/add-widget.md) for that flow.

## Prerequisites

- Node ≥ 20 — for `canshift-core`, `canshift-tuner`, `canshift-mobile`, and this site.
- PlatformIO Core — for `canshift-firmware`. `pip install platformio`.
- Rust toolchain (xtensa) — optional, only if you touch the firmware's Rust modules.

## Clone as siblings

Clone each repository you need side by side under a common parent. The layout
matters: the firmware reads `../canshift-core` for schema parity when the
checkout is present, and each repository's `npm install` arms its own
pre-commit hooks.

```bash
mkdir CANShift && cd CANShift
git clone https://github.com/CANShift/canshift-core.git
git clone https://github.com/CANShift/canshift-firmware.git
git clone https://github.com/CANShift/canshift-tuner.git
```

You only need the repositories you are working on. `@canshift/core` is
published to npm, so the tuner and mobile app resolve it from there — a local
`canshift-core` checkout is only required when you are changing the contracts
themselves.

## Per-repository setup

### `canshift-core`

```bash
cd canshift-core
npm install        # arms the pre-commit hooks
npm run build      # tsc → dist/; consumers resolve dist only
npm test           # Jest
```

Consumers (tuner, mobile) install `@canshift/core` from npm. A schema change
lands here first, is published, then the consumers bump their dependency.

### `canshift-firmware`

```bash
cd canshift-firmware
pio run -e debug   # dev build
pio test -e native # native test suite
```

There is no `npm install` step here; PlatformIO manages dependencies. When a
`../canshift-core` checkout exists as a sibling it wins for schema parity;
otherwise the firmware falls back to the version pinned in
`core-schema-version.txt`. Update that pin on every schema bump. clang-format
gates every PR — run `pio run -t format` before committing.

### `canshift-tuner`

```bash
cd canshift-tuner
npm install        # @canshift/core comes from npm; arms the pre-commit hooks
npm run dev        # vite on http://localhost:5173 (simulation mode without a device)
```

Runs in simulation mode without a device. WebSerial (live device + flasher)
needs a Chromium browser.

### `canshift-mobile`

Deferred — build only when you are working on mobile.

```bash
cd canshift-mobile
npm install
npx expo prebuild --clean --platform ios
npm run ios
```

### `canshift-docs`

```bash
cd canshift-docs
npm install        # arms the pre-commit hooks
npm run dev        # http://localhost:4321
```

## Branches, commits, PRs

Every repository shares the same workflow:

- **Branch** off the default branch as `type/short-description` — e.g.
  `feat/cruise-l-shape`, `fix/token-refresh`. No repository segment in the
  name; the repository is already the scope.
- **Conventional Commits**, subject line only, no body:

  ```
  feat: cruise L-shape buttons
  fix: overflow on long signal names
  docs: rewrite contributing guides for the multi-repo layout
  ```

- **Open a PR** with `gh pr create` — never merge directly to `main`. Each
  repository merges via **rebase and merge** once its required checks pass.

## Per-repository CI

Checks run in the repository you push to — there is no shared pipeline:

| Repository          | Required checks                             |
| ------------------- | ------------------------------------------- |
| `canshift-core`     | `lint`, `test`, `build`, `firmware-parity`  |
| `canshift-firmware` | `lint` (clang-format), native build + tests |
| `canshift-tuner`    | `lint`, `typecheck`, `test`, `build`        |
| `canshift-mobile`   | `lint`, `typecheck`, `test`                 |
| `canshift-docs`     | `lint`, `build`                             |

`firmware-parity` checks out `canshift-firmware` alongside `canshift-core` and
fails the PR if the schema the firmware pins in `core-schema-version.txt` has
drifted from what core exports — so a schema change and its firmware pin land
together, not a release apart.

See [Testing](../contributing/testing.md) for the test harnesses and
[Release process](../contributing/release-process.md) for how firmware
releases and the `@canshift/core` npm package are cut.
