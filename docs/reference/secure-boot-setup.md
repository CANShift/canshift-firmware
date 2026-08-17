# Secure boot setup

Source: [`scripts/secure_boot_first_flash.sh`](../../scripts/secure_boot_first_flash.sh)

> [!CAUTION]
> **DO NOT RUN ON DEV BOARDS.** Burning eFuses is **irreversible**. A wrong
> step bricks the chip permanently — there is no recovery, you replace the
> module. **Read this entire document, end to end, before invoking any
> script in `canshift-firmware/scripts/secure_boot_*`.** The scripts are
> guarded by an explicit `--i-understand-this-is-irreversible` flag and a
> 60-second confirmation prompt. Do not bypass them.

This document describes the opt-in path for enabling ESP32 Secure Boot v2
and Flash Encryption on production CANShift devices. Today's dev fleet
boots **unsigned** images and runs **unencrypted** flash. Enabling these
features is a one-way decision per chip and is performed exactly once,
during the production-flash procedure for that specific unit.

The original PR that introduced this guide shipped only documentation,
an opt-in PlatformIO env, and gated host-side scripts. **No runtime code
path is touched** — the existing unsigned/unencrypted production firmware
keeps building byte-for-byte identical to before. Adoption is per-device,
by an operator, in a controlled environment.

#### Rollout state (updated #531)

| Capability                                            | State                                                                                                                                                                     |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[env:secure]` PlatformIO env                         | Ready — partition table aligned to `ota_4mb_wifi.csv` post-#1117                                                                                                          |
| Anti-rollback floor (`CONFIG_BOOTLOADER_APP_SEC_VER`) | `2` — see section 7                                                                                                                                                       |
| CI build of `[env:secure]`                            | Non-blocking job `firmware-secure-build` in `.github/workflows/ci.yml`, gated on `SECURE_BOOT_SIGNING_KEY_TEST` repo secret being present (otherwise skips with a notice) |
| Project signing key                                   | **Not yet generated.** Run `scripts/generate_keys.sh` on a controlled workstation                                                                                         |
| First-flash on a real chip                            | **Not yet performed.** Gated on sacrificial-board QA                                                                                                                      |
| Tuner USB-flasher signed-build support                | **Not yet.** The built-in browser flasher writes raw bytes — signed flashing is out of scope for it today                                                                 |

Everything below this point is the operational reference for when those
remaining items are picked up.

---

## 1. Threat model

### What this protects against

- **Unsigned firmware execution.** Secure Boot v2 verifies an RSA-3072
  signature on the bootloader (signed by the Espressif ROM via burned
  pubkey digest) and on every app image (signed by our private key, hash
  verified against a digest fused into the chip). An attacker who flashes
  a tampered or third-party image over USB or OTA cannot boot it — the
  ROM bootloader rejects the image before `app_main` runs.
- **Flash dump attack.** Flash Encryption (AES-XTS-256, key in eFuse
  BLOCK1) means `esptool.py read_flash` returns ciphertext. An attacker
  with physical access who pulls the SPI flash chip cannot recover
  firmware, NVS contents, SPIFFS configs, or the per-device OTA HMAC
  key (stored in NVS since #521).
- **OTA downgrade attack.** Combined with `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`
  and a per-release-incremented `CONFIG_BOOTLOADER_APP_SEC_VER`, the chip
  refuses to boot any signed image with a security-version lower than the
  one currently committed. Fielded devices cannot be rolled back to a
  vulnerable build even by an attacker who has captured an older signed
  artifact. (This guide flags the anti-rollback wiring as follow-up work
  — not enabled in this PR.)

### What this does NOT protect against

- **Decapping / microprobing.** A well-funded attacker with chip-level
  tooling can extract eFuse contents from the silicon. ESP32 is a
  consumer-grade automotive HMI part, not a tamper-resistant secure
  element. Treat per-chip flash keys as "raises the bar," not "secret."
- **SPI-bus sniff before encryption is enabled.** Currently fielded dev
  boards run unencrypted flash. Anyone with bus-level access has already
  had the opportunity to read the firmware off them. Secure boot does not
  retroactively protect those units — they remain in the original posture.
  Only newly provisioned production units gain protection.
- **Key-custody compromise.** If the RSA-3072 secure-boot signing key
  leaks, an attacker can sign arbitrary images that every fielded device
  will accept. The blast radius is **the entire fleet**. The signing key
  must live offline (HSM, Yubikey, encrypted disk on a dedicated, network-
  isolated workstation). It must never enter CI in this initial rollout.
- **Side channels.** Power analysis, EM emanation, and timing attacks
  against AES-XTS are out of scope for ESP32. If your threat model
  includes them, ESP32 is the wrong part.
- **An attacker who legitimately holds the signing key.** Insider threat
  is not addressed by cryptography.
- **Anything before secure boot is enabled.** A development unit running
  an unsigned image today is not protected, period. Secure boot is only
  meaningful from the moment fuses are burned — for that specific chip,
  going forward.

### What this trades

- **Brick risk.** Wrong key digest, interrupted fuse burn, flash key
  burned without an encrypted image: chip becomes a paperweight. There is
  no JTAG fallback once `JTAG_DISABLE` is burned. Always assume the chip
  in front of you is one mis-step from the bin.
- **OTA recovery loss.** Once secure boot is on, the bootloader is signed
  and immutable. A bug in the bootloader that gets through QA cannot be
  fixed in the field — the device is a paperweight. Test the secure-boot
  bootloader exhaustively before signing it for production.
- **Lost signing key = frozen fleet.** Every device fielded under that
  key is permanently stuck on its last-flashed image. There is no
  recovery key, no Espressif master key, no escrow with the vendor.
  **Custody discipline is the single most important operational concern.**

---

## 2. Prerequisites

### Tooling

The PlatformIO Espressif toolchain ships the two utilities required:

- `~/.platformio/packages/tool-esptoolpy/espsecure.py` — generate signing
  keys, sign images.
- `~/.platformio/packages/tool-esptoolpy/espefuse.py` — read and burn
  eFuses.

Both must be invoked through the same Python environment that PlatformIO
uses (the wrapper handles `PYTHONPATH`). If you are running from a fresh
machine, install PlatformIO Core, then run `pio run -e crowpanel_28` once
to populate the toolchain cache.

### Secure-boot signing key — RSA-3072, off-Git, **per project**

Generated **once** for the whole project and reused across every device
the project ships. Stored at `canshift-firmware/secrets/secure_boot_signing_key.pem`,
which is gitignored. Generated via `scripts/generate_keys.sh`.

**Custody:** see Section 6.

### Flash encryption key — AES-XTS-256, off-Git, **per chip**

Generated **per device** during first flash. The script writes the raw
256-bit key to `keys/flash_<chip-mac>.bin` on the host. The same script
appends `<chip-mac>,<flash-key-sha256>,<timestamp>,<operator>` to
`keys/escrow.csv` so that you can later identify which device shipped
with which key (the key itself is not in escrow.csv — only its hash).

The key file must be retained out-of-band for that chip. Losing it does
**not** brick the chip (the chip can still decrypt itself), but it
prevents you from re-flashing pre-encrypted images from this host. In
practice, since the chip itself encrypts on `write_flash --encrypt`, the
host-side copy is mostly an audit artifact.

### Partition table

`canshift-firmware/ota_4mb_secure.csv` — sibling to `ota_4mb_wifi.csv`
(aligned post-#1117 / #531). Adds:

- A 4 KB `nvs_keys` partition for encrypted-NVS key storage (encrypted
  separately under a key in eFuse).
- `encrypted` flag set on `nvs`, `nvs_keys`, `otadata`, `app0`, `app1`,
  `spiffs`.

Layout identity vs `ota_4mb_wifi.csv`: app slots stay at `0x1B0000`
(1728 KB) each, SPIFFS stays at `0x80000` @ `0x370000`, coredump at
`0x10000` @ `0x3F0000`. The only geometry change is `nvs` shrinking
from 20 KB to 16 KB to make room for the 4 KB `nvs_keys` partition.
That alignment means a fielded dash on the post-#1117 layout can move
to a signed/encrypted image via a one-shot USB reflash without
relocating any data partition, and the SPIFFS offset (`0x370000`) is a
single constant across every host-side flasher.

The bootloader and partition table themselves stay plaintext (signed,
not encrypted) — Espressif's ROM reads them in the clear.

### Sdkconfig overrides

`canshift-firmware/sdkconfig.defaults.secure` — applied automatically
when building `[env:secure]` via `board_build.cmake_extra_args`. Sets:

```
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y
CONFIG_SECURE_BOOT_SIGNING_KEY="secrets/secure_boot_signing_key.pem"
CONFIG_FLASH_ENCRYPTION_ENABLED=y
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y
CONFIG_BOOTLOADER_APP_SEC_VER=2
```

`MODE_RELEASE` is mandatory for production — `MODE_DEVELOPMENT` allows a
limited number of plaintext re-flashes via UART (`FLASH_CRYPT_CNT < 0x7F`)
which negates the protection.

---

## 3. One-way fuse map

The first-flash script burns the following eFuses, in order. **Each row
is irreversible.** Read the warning above the row, then read it again.

> [!CAUTION]
> Steps below are listed in burn order. **Each row is one-way.** Failed
> burn between any two rows leaves the chip in an indeterminate state —
> the only recovery is chip replacement.

### Step 1 — `BLOCK2 ← SHA-256(secure-boot RSA pubkey)`

> [!CAUTION]
> One-way. Wrong digest = chip refuses to boot any image, ever, including
> the bootloader you just flashed. Verify the pubkey digest against the
> private key in `secrets/secure_boot_signing_key.pem` **twice** before
> burning.

Burns the SHA-256 of the RSA-3072 secure-boot pubkey into eFuse BLOCK2.
The bootloader-side check uses this digest to verify the bootloader's own
signature; the bootloader then verifies app images.

### Step 2 — `ABS_DONE_1` / `SECURE_BOOT_EN`

> [!CAUTION]
> One-way. Once burned, the ROM enforces signature checks on every image
> and bootloader, forever. If `BLOCK2` was burned with the wrong digest,
> burning `ABS_DONE_1` is the moment the chip becomes a paperweight.

Activates secure boot v2 enforcement at the ROM level.

### Step 3 — `BLOCK1 ← AES-256 flash-encryption key`

> [!CAUTION]
> One-way. Wrong key = chip can no longer decrypt its own flash and will
> reset on the first read of an encrypted partition.

Burns the per-chip AES-XTS-256 flash encryption key.

### Step 4 — `FLASH_CRYPT_CNT = 0x7F`

> [!CAUTION]
> One-way (it's a write-once monotonic counter; setting it to `0x7F`
> consumes all 7 bits). Once set, the chip will never again accept a
> plaintext flash write — every `write_flash` must be preceded by
> `--encrypt` or it will produce garbage.

Locks flash encryption into release mode.

### Step 5 — `DIS_DOWNLOAD_MANUAL_ENCRYPT` + `DIS_DOWNLOAD_DCACHE` + `DIS_DOWNLOAD_ICACHE`

> [!CAUTION]
> One-way. Disables the UART download mode's ability to access flash via
> the ROM cache, closing the documented bypass for flash encryption.

Cuts the UART-mode encrypted-flash plaintext-read backdoor.

### Step 6 — `JTAG_DISABLE`

> [!CAUTION]
> One-way. Permanently disables the JTAG TAP. After this fuse is burned,
> there is no debugger access for the lifetime of the chip. Make
> absolutely sure the firmware works before this step.

### Step 7 — `DIS_USB_JTAG`

> [!CAUTION]
> One-way. Disables the USB-Serial-JTAG bridge, where present. ESP32-S3
> and later only — the original ESP32 (CrowPanel 2.8") does not expose
> USB-JTAG, so this step is a no-op there. The script detects chip type
> and skips it as appropriate.

### Failed-burn recovery

**There is none.** Replace the chip. Document the failure in
`keys/escrow.csv` (post-mortem column) so the unit's serial is not
treated as a deployed device.

---

## 4. First-flash production procedure

The procedure is a single script invocation. **Do not run it on a dev
board.** Use only on units destined for the field.

```bash
cd canshift-firmware
./scripts/secure_boot_first_flash.sh --i-understand-this-is-irreversible
```

The script's prompts, in order:

1. Banner: "DO NOT RUN ON DEV BOARDS — 60 seconds to abort." Sleeps 60 s,
   echoing a tick every 5 s. Ctrl-C aborts cleanly.
2. Displays chip ID (`espefuse.py summary --json` → `mac`), operator
   email (`git config user.email`), and the path to `secrets/`. Prompts
   `confirm? (type the chip MAC):` — must match exactly.
3. Pre-flight: `espefuse.py summary` and refuses to proceed if any of
   `ABS_DONE_1`, `FLASH_CRYPT_CNT`, `BLOCK1`, `BLOCK2` are non-zero. A
   partially-fused chip is considered already-bricked and is rejected.
4. Generates per-chip flash key into `keys/flash_<chip-mac>.bin`. Aborts
   if the file already exists (refuses to overwrite).
5. Verifies `secrets/secure_boot_signing_key.pem` exists. Aborts and
   directs operator to `generate_keys.sh` if missing.
6. `pio run -e secure -t buildprog` — produces `bootloader.bin`,
   `partitions.bin`, and the signed app image at
   `.pio/build/secure/firmware.bin`. Bootloader signing happens at build
   time using the key under `secrets/`.
7. `esptool.py write_flash --encrypt` for `nvs`, `otadata`, `app0`,
   `app1`, `spiffs`. Bootloader and partition table written plaintext
   (they are signed, not encrypted).
8. Burn fuses in order. **Pause for operator confirmation between Step 2
   and Step 3, and again between Step 4 and Step 5.** This gives the
   operator a chance to verify the chip behaviour at intermediate states
   before locking the next door.
9. Append `<chip-mac>,<flash-key-sha256>,<timestamp>,<operator>` to
   `keys/escrow.csv`.
10. Final verification: `espefuse.py summary` — reports the burned state.
    Power-cycle the device. Capture the boot log, confirm it reaches
    `app_main`, and confirm the secure-boot v2 banner is present in the
    ROM bootloader output.
11. Print the escrow reminder: "Move `keys/flash_<mac>.bin` to offline
    storage before the next chip."

The script aborts cleanly on Ctrl-C, on `set -euo pipefail` failures, and
on operator-prompt mismatch. It never auto-confirms.

---

## 5. Operating the `[env:secure]` build

Without running the production flash, you can dry-run the signed build
locally to verify it produces a valid signed binary:

```bash
cd canshift-firmware
./scripts/generate_keys.sh --i-understand-this-is-irreversible   # once per project
pio run -e secure
```

Verify the signed image:

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py image_info \
    --version 2 \
    .pio/build/secure/firmware.bin
```

The output reports `Secure Boot v2: enabled` and shows the RSA signature
trailer.

This build is **only** for verification — flashing it via plain
`pio run -e secure -t upload` to a non-fused chip works (the chip
ignores the signature when secure boot is off), but flashing it to a
fused chip without `--encrypt` will brick the chip after one boot
(`FLASH_CRYPT_CNT` increments on every plaintext write of an encrypted
partition until the chip refuses to boot). Always go through the
first-flash script for fused chips.

---

## 6. Key custody

**The signing key is the most security-critical artifact in the project.**

### Recommended

- **HSM** (YubiHSM 2, NitroKey HSM 2, AWS CloudHSM): the key never leaves
  the device. `espsecure.py` does not natively support HSM signing in
  v4.x; the workaround is a Python wrapper that pre-computes the SHA-256
  digest of the image, sends it to the HSM for signing, and splices the
  signature back into the image. Documented as follow-up work.
- **YubiKey PIV slot 9c**: similar workflow, signs the digest only.
  Easier to deploy than HSM, requires a single physical device per
  signer.
- **Encrypted disk on dedicated workstation**: at minimum, the key sits
  on an air-gapped or network-isolated machine, in a LUKS / FileVault
  encrypted volume, accessible only by the signer. Backups are encrypted
  (age, GPG) and stored offline (USB stick in a safe). Two-person
  control: backup custodian and key holder are different people.

### Anti-recommended

- **Do not commit to Git, ever.** `.gitignore` blocks `secrets/`, `keys/`,
  and `*.pem` defensively. Verify with `git check-ignore secrets/`.
- **Do not put the key in CI.** The first rollout of secure boot
  intentionally has zero CI involvement. A future, separate PR may add
  tag-gated signing in `release.yml`, with the private key stored as a
  GitHub Actions OIDC-gated secret on the canonical signing repo. That
  PR will land with its own threat-model review.
- **Do not store the key in a cloud-synced folder** (Dropbox, iCloud,
  Drive). Cloud-side compromise = key compromise = fleet compromise.

### Failure mode: lost signing key

**Every fielded device under that key is permanently stuck on its
current image.** No future OTA update will be accepted — they all fail
the signature check. The fleet is frozen. The only mitigation is to
treat the signing key like the keys to the bank vault.

### Failure mode: stolen signing key

The attacker can sign arbitrary firmware that every fielded device will
accept. There is no revocation. The mitigation is to keep the key
behind hardware (HSM / YubiKey) so theft requires physical compromise
of the signer's environment plus the PIN/PIV credential.

---

## 7. OTA implications

### Existing OTA pipeline (today)

`canshift-firmware/src/hal/wifi/ota_hmac.cpp` appends a 32-byte HMAC-SHA256
trailer to every uploaded image. The firmware verifies the HMAC against
a key it resolves on first boot before accepting the image. This is
**transport-layer integrity** — it proves the image came from a party
that holds the shared HMAC secret.

#### Key provisioning (#521 — per-device NVS key)

The verification key resolves through a three-step fallback chain at boot:

1. **NVS hit** — read 32 bytes from namespace `ota`, key `hmac_key`. This
   is the steady state for any device that booted at least once after
   the #521 rollout. The key is unique per device — leaking one chip's
   key does not compromise the fleet.
2. **NVS miss (first boot)** — generate 32 fresh bytes via
   `esp_fill_random()`, persist them to NVS, return them. Subsequent
   boots take path 1.
3. **NVS write failure** — fall back to the build-time `OTA_HMAC_SECRET`
   macro injected by `scripts/extra_targets.py` from `secrets.ini`. This
   keeps **legacy installs working** through the rollout window: a dash
   running pre-#521 firmware that gets upgraded reads no NVS entry,
   tries to generate, and lands on the embedded key if the NVS partition
   is somehow unwritable. This branch is the only reason the embedded
   macro still exists; remove it once the fleet has rolled over.

At boot the firmware emits a single diag line tagged `OTA` of the form:

    [I][OTA] HMAC key source=NVS sha256=a3f1b2c0
    [I][OTA] HMAC key source=NVS (generated) sha256=...
    [I][OTA] HMAC key source=embedded (legacy) sha256=...

The 8-hex prefix is the leading 4 bytes of `SHA-256(key)`. The actual key
bytes never reach logs. Operators eyeballing a field dash can tell at a
glance which provenance the device is on without compromising the secret.

### With secure boot v2 enabled

The OTA flow changes posture:

1. **`Update.end(true)`** triggers the bootloader's RSA-3072 signature
   verification on the next boot. An image without a valid signature
   does not boot — the chip falls back to the previous slot via
   `otadata`. No code change needed in `ota_hmac.cpp`; the bootloader
   handles it.
2. **HMAC trailer becomes a pre-flight check.** It still runs as a
   defense-in-depth gate at upload time — rejecting a malformed image at
   transport rather than letting the bootloader catch it post-write.
   Useful for rate-limiting and for surfacing failures to the user
   before a reboot.
3. **Anti-rollback** (`CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y`) requires
   bumping `CONFIG_BOOTLOADER_APP_SEC_VER` per-release in the
   `sdkconfig.defaults.secure` file. The bootloader stores the highest
   `SEC_VER` ever booted in eFuse and refuses to boot any image with a
   lower version. The floor is currently `2` (bumped in #531 to leave
   headroom for an emergency v1 → v2 downgrade cutoff). The per-release
   ratchet — "what triggers a SEC_VER bump on the next signed build" — is
   still maintainer-driven, not CI-enforced: bump only when the release
   fixes a remotely exploitable bug, because every bump permanently
   raises the floor on every chip that boots the new image.
4. **Bootloader is signed but not OTA-updatable.** The bootloader sits in
   the boot region, which `Update.h` does not write. A bootloader bug
   that survives QA is a production incident with no remote fix.

---

## 8. Recovery / brick risk matrix

| Failure                                                     | Effect                                                                                                                                        | Recovery                                                                                                                                              |
| ----------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Wrong RSA pubkey digest burned to BLOCK2                    | Chip rejects every image, including its own bootloader                                                                                        | None — replace chip                                                                                                                                   |
| `BLOCK1` flash key burned, no encrypted image written first | Chip can't decrypt boot regions on power-up                                                                                                   | None — replace chip                                                                                                                                   |
| Operator interrupts the script between Step 2 and Step 3    | `ABS_DONE_1` set, no flash key — chip will boot signed images plaintext, but `FLASH_CRYPT_CNT` will increment on next encrypted write attempt | Resume the script (Step 3 onward) on the same chip; do not skip                                                                                       |
| Operator interrupts between Step 4 and Step 5               | `FLASH_CRYPT_CNT=0x7F`, but UART download backdoor still open                                                                                 | Resume the script (Step 5 onward); attacker window is the time between burns                                                                          |
| Lost signing key                                            | Every fielded device frozen at current image                                                                                                  | None — long-term fix is to ship a final image that adds a co-signing path before the loss                                                             |
| Lost per-chip flash key (host-side `keys/flash_<mac>.bin`)  | Cannot pre-encrypt new images for that specific chip from this host                                                                           | Use the chip's own `write_flash --encrypt` flow (chip encrypts in place); host-side key is an audit artifact, not strictly required after first flash |
| Bricked chip from any of the above                          | Module is e-waste                                                                                                                             | Replace; update `keys/escrow.csv` post-mortem column                                                                                                  |

---

## 9. Acceptance + verification

The scaffolding PRs are considered done when:

- `pio run -e crowpanel_28` continues to build cleanly, unchanged.
- `pio run -e sim` continues to build cleanly, unchanged.
- `pio run -e secure` builds (after running `generate_keys.sh`) and
  produces a signed image. `esptool.py image_info --version 2` reports
  `Secure Boot v2: enabled`.
- The CI job `firmware — secure-boot build (non-blocking)` runs end to
  end on every firmware-touching PR when the
  `SECURE_BOOT_SIGNING_KEY_TEST` secret is provisioned, and skips with
  a notice when it is not.
- `secure_boot_first_flash.sh` invoked without args prints the safety
  banner and exits with status 1.
- `secure_boot_first_flash.sh` invoked with `DEV_BOARD_DO_NOT_RUN=1`
  exits with status 1 immediately.
- `generate_keys.sh` invoked twice (with the flag) refuses the second
  invocation because `secrets/secure_boot_signing_key.pem` already
  exists.
- `git check-ignore secrets/` prints the path (i.e., is gitignored).

End-to-end verification on a real chip is **still out of scope**. The
on-hardware acceptance test (sacrificial board → confirmed signed boot →
documented recovery procedure run at least once) is an Issue #531
follow-up that runs on a dedicated, known-disposable chip.

---

## 10. Out-of-scope future work

- **Tag-gated CI signing of release artifacts.** The `ci.yml`
  `firmware-secure-build` job (added in #531) compiles `[env:secure]`
  on every firmware-touching PR with a TEST signing key, but the
  `release.yml` workflow does NOT yet sign the published release binary
  with the production key. That requires a tag-only job
  (`if: startsWith(github.ref, 'refs/tags/v')`) plus a hardened secret
  store — GitHub Actions OIDC + dedicated signing repo with restricted
  branch-protection rules. Production signing key must stay out of the
  main repo's secrets.
- **Per-chip key escrow tooling.** Today `keys/escrow.csv` is a flat
  text file. A future PR may move escrow into a signed, append-only
  log on a dedicated host (e.g., a small SQLite database with a
  per-row HMAC chain) so that escrow tampering is detectable.
- **Per-release SEC_VER ratchet automation.** The `sdkconfig.defaults.secure`
  floor is currently `2` (#531 rollout-readiness bump), and bumping is
  maintainer-driven. A future PR may add a release-tag hook that fails
  if a release labelled `security:remote-exploit-fix` ships without
  bumping the floor, so the per-release ratchet does not depend on
  reviewer memory.
- **Tuner USB-flasher signed-build support.** The built-in browser
  flasher writes raw bytes via `esptool-js` — it has no `write_flash
--encrypt` path, no signed-bootloader awareness, and no `nvs_keys`
  partition handling. Signed builds today are flashable only via
  `scripts/secure_boot_first_flash.sh` on a controlled workstation. A
  future version of the flasher would need to pre-encrypt payloads on
  the host side before pushing them through Web Serial; file an issue
  when the project commits to the secure-boot rollout on real chips.
- **HSM-backed signing.** `espsecure.py` 4.x does not natively support
  HSM. The wrapper that pre-computes the digest and splices the
  signature is a self-contained Python module — TBD.
- **Bootloader-update path.** Espressif documents an off-label
  technique to OTA-update a signed bootloader by writing into the
  app slot, then having the running app rewrite the bootloader region.
  This is fragile and risky; production CANShift will not adopt it
  without a strong driver.
