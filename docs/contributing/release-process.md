# Release process

Firmware releases ship from
[`CANShift/canshift-firmware`](https://github.com/CANShift/canshift-firmware).
This docs site pulls those GitHub Releases at build time and renders the
public [changelog](https://github.com/CANShift/canshift-firmware/releases), so release descriptions need to be
substantive. The other repositories version and ship independently — there is
no global lockstep.

## What ships from where

- **Firmware** — GitHub Releases on `canshift-firmware`, with signed binaries
  attached. This is the release stream the changelog renders.
- **`@canshift/core`** — published to npm. Tagging `vX.Y.Z` on
  `canshift-core` runs the publish workflow, which ships to npm with
  provenance. The npm version is independent of the schema version.
- **Tuner** — deployed continuously by Vercel (preview per PR, production on
  `main`). No release tag; whatever is on `main` is live.
- **Mobile** — store builds via EAS when it comes out of deferral. Not part of
  the firmware release stream.

## Versioning

- **Firmware**: `canshift-firmware/package.json::version` — the build bakes it
  into `APP_VERSION_STR`. Semver; still pre-1.0.
- **Core schema**: `CURRENT_SCHEMA_VERSION` in `canshift-core` — bumped only
  when the config shape changes in a way the firmware must reject old configs
  for, always paired with a migration in `src/migrations/registry.ts` in the
  same PR. Independent of both firmware semver and the `@canshift/core` npm
  version. The firmware pins the schema version it expects in
  `core-schema-version.txt` — update that pin on every bump.
- **Core npm package**: `canshift-core/package.json::version` — bumped when you
  want consumers to be able to pull new contracts. Independent of the schema
  version.

The firmware semver bumps the most often. The schema version is the gate that
forces a re-burn from the tuner.

## Release-notes structure

Every firmware release description follows this template (also documented in
[Changelog](https://github.com/CANShift/canshift-firmware/releases)):

```markdown
## [Version] — YYYY-MM-DD

### 🚗 For the driver

- (UX, new widgets, hardware support, etc.)

### 🔧 For the tuner / installer

- (config, calibration, ECU parameters)

### 🔬 Firmware / dev

- (build flags, schema, Rust modules, refactors)

### ⚠️ Breaking

- (concrete impact + required action — re-flash, re-burn config, etc.)

### PRs

- #XX — ...
```

> [!WARNING]
> **Don't ship empty**
>
> A release reduced to "see commit log" or a bare list of PR titles renders as
> a useless changelog entry. Group by audience; lead with what the driver
> notices.

## Cutting a firmware release

The release is driven by merging a version bump — the workflow does the
tagging and building. You are not tagging by hand.

1. **Bump the firmware version** in `canshift-firmware/package.json`. Open a PR
   titled `chore: bump to <version>` and merge it to `main`.

2. **Bump the schema version** first, in a separate `canshift-core` PR, if any
   breaking change to the config shape landed since the last release — pair it
   with a migration and update `core-schema-version.txt` in the firmware.
   Otherwise leave it alone.

3. **Let the release workflow run.** On the version-bump merge, the workflow
   tags the commit, builds the signed firmware artifacts (merged / firmware /
   SPIFFS), and drafts a GitHub Release for the tag.

4. **Fill in the release notes.** Edit the drafted release, paste the
   structured template above, and write real content from the PR stream since
   the previous release. Then publish it.

5. **Confirm the flasher sees it.** The tuner's Firmware view reads the GitHub
   Releases API on `canshift-firmware` and lists available versions — the new
   release appears there within about a minute.

6. **Smoke-test** by re-flashing a dash from the tuner using the new release.

## What each artifact is, and where it goes

Three binaries per released board, all listed in `manifest.json` with their SHA-256:

| Artifact        | Contents                                         | Flashed at                                           |
| --------------- | ------------------------------------------------ | ---------------------------------------------------- |
| `-merged.bin`   | bootloader + partition table + app, in one image | `0x0`, always                                        |
| `-firmware.bin` | the app partition alone                          | `0x10000` — this is what OTA consumes                |
| `-spiffs.bin`   | the data partition (config, fonts, assets)       | the `spiffs` offset from the board's partition table |

The merged image is always written at `0x0` whatever the chip, because
`esptool merge_bin` emits from target offset 0 and pads. What changes per chip is
the **bootloader's position inside** that image:

| Chip                            | Bootloader offset |
| ------------------------------- | ----------------- |
| `esp32`                         | `0x1000`          |
| `esp32s2`, `esp32s3`, `esp32c3` | `0x0`             |

The workflow derives it from the board's `chip` field and **fails the release** on
a chip it has no offset for, rather than defaulting. Getting this wrong is silent:
esptool accepts the layout, and the board simply never boots — the ROM finds
`0xFF` where it expects the image magic. A new chip family means teaching that
`case` before its first release.

The app partition sits at `0x10000` and the partition table at `0x8000` on every
board we ship, in both `ota_4mb.csv` and `ota_16mb.csv`.

## Patch vs minor vs major

- **Patch** (x.y.z → x.y.z+1): bug fix, no UX change, no schema change.
  Re-flash is optional unless the user hit the bug.
- **Minor** (x.y → x.y+1): new feature, no breaking change. Re-flash
  recommended.
- **Major** (breaking): driver MUST re-flash AND re-burn config. Spell out the
  migration in the `⚠️ Breaking` section.

## Pre-releases

Bump to a version with a pre-release suffix (e.g. `1.3.0-rc.1`). GitHub
Releases recognises the suffix and badges it as "pre-release"; the tuner's
Firmware view lists pre-releases behind a toggle.

## Publishing `@canshift/core`

Core is released separately from the firmware:

1. Bump `canshift-core/package.json::version` and merge to `main`.
2. Tag the release commit `vX.Y.Z` and push the tag — the publish workflow
   ships the package to npm with provenance.
3. Bump the `@canshift/core` dependency in `canshift-tuner` (and
   `canshift-mobile` when active) to pick up the new contracts.
