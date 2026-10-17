# Secure boot setup

Sources: [`secure-boot/esp32s3/`](../../secure-boot/esp32s3/),
[`scripts/secure_boot_bootloader.sh`](../../scripts/secure_boot_bootloader.sh),
[`scripts/secure_boot_sign.sh`](../../scripts/secure_boot_sign.sh),
[`ota_16mb_secure.csv`](../../ota_16mb_secure.csv)

> [!CAUTION]
> Enabling secure boot v2 and flash encryption burns eFuses. **eFuses cannot be
> un-burned.** A chip that has booted the secure bootloader once is a secure
> chip for the rest of its life. It runs only images signed by one key, and it
> no longer lets anyone read its flash or eFuses over USB. Every step below is
> marked **REVERSIBLE** or **IRREVERSIBLE**. Read the whole page before you run
> anything, and do the first run on a board you are prepared to lose.

Secure boot is **prepared, not enabled**. No release ships a secure image, no
dash is fused, and the default build is unchanged. It is the Waveshare
ESP32-S3 (`waveshare_s3_28`) only. The CrowPanel `[env:secure]` builds no secure
boot image at all: see [the CrowPanel](#the-crowpanel-envsecure).

## What it protects, and what it does not

| Threat                                                         | Without                                      | With secure boot v2 + flash encryption                                                        |
| -------------------------------------------------------------- | -------------------------------------------- | --------------------------------------------------------------------------------------------- |
| Someone flashes their own firmware over USB with `esptool`     | It boots                                     | The bootloader refuses it at boot                                                             |
| Someone OTA-uploads their own firmware                         | Refused by the Ed25519 check                 | Refused by the Ed25519 check, and by the bootloader if that check were ever bypassed          |
| Someone reads the firmware back over USB                       | `esptool read-flash` returns it              | Secure download mode refuses the read                                                         |
| Someone desolders the flash chip and reads it                  | Firmware, settings, SPIFFS, all in plaintext | App slots, otadata and coredump are ciphertext. **NVS and SPIFFS stay plaintext** (see below) |
| Someone attaches JTAG                                          | Open                                         | Disabled in eFuse                                                                             |
| Someone replays an older, genuinely signed release (downgrade) | Accepted                                     | **Still accepted.** No anti-rollback (see [Known gaps](#known-gaps))                          |
| The signing key leaks                                          | —                                            | Whoever has it can sign images every fused dash accepts. Nothing can revoke it                |
| The signing key is lost                                        | —                                            | Fused dashes keep running but never accept another image. Not over USB, not over the air      |

NVS holds settings, touch calibration, BLE bonds and the board profile. SPIFFS
holds config, fonts and icons. Neither holds a secret, and neither library can
work on encrypted flash, so they stay plaintext in
[`ota_16mb_secure.csv`](../../ota_16mb_secure.csv). Encrypting NVS would need
NVS encryption compiled into the app, which the prebuilt Arduino libraries do
not have.

## How the secure build is put together

`framework = arduino` ships the bootloader and the ESP-IDF libraries prebuilt
(arduino-esp32 2.0.14 on ESP-IDF v4.4.6). Both have secure boot and flash
encryption compiled out. PlatformIO's Arduino builder ignores `sdkconfig`
overrides, so no setting in `platformio.ini` can turn them on. The secure build
therefore has three parts:

| Part         | Built by                                                          | Notes                                                                                            |
| ------------ | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| App          | `pio run -e waveshare_s3_28_secure`                               | The normal S3 app against `ota_16mb_secure.csv`. `pio upload` is refused in this env             |
| Bootloader   | `scripts/secure_boot_bootloader.sh esp32s3 <out>`                 | ESP-IDF v4.4.6 in Docker (image pinned by digest), from `secure-boot/esp32s3/sdkconfig.defaults` |
| Both, signed | `scripts/secure_boot_sign.sh <key> <bootloader> <firmware> <out>` | Signs, verifies against the extracted public key, checks the bootloader fits, prints the digest  |

The bootloader is configured with:

- Secure boot v2 with RSA-3072 (RSA-PSS). This is the only scheme ESP-IDF v4.4
  supports on the S3.
- Flash encryption in **release** mode, XTS-AES-128 with a 256-bit key.
- Secure download mode. After the first boot, `esptool` can still write flash
  and report security info, but it can no longer read flash or eFuses.
- App rollback, the same as the prebuilt bootloader, so `verifyRollbackLater()`
  still works ([Boot sequence](../architecture/boot-sequence.md#ota-mark-valid-placement)).
- Log level `NONE`. The prebuilt app reads the partition table at `0x8000`, so
  the signed bootloader has to fit below it. Signed, it is `0x7000` at `NONE`.
  At `WARN` it would be `0xA000`, which does not fit. `secure_boot_sign.sh`
  fails the build if it ever grows past `0x8000`. So **the secure bootloader
  prints nothing**: the first sign of life is the app's own output.
- `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=n`: the key is never needed at
  build time, only at signing.

## Two signatures, in a fixed order

A secure release image carries two signatures, checked by two different
parties:

| Signature     | Key                                 | Where it lives                                | Checked by                  | When                             |
| ------------- | ----------------------------------- | --------------------------------------------- | --------------------------- | -------------------------------- |
| Secure boot   | RSA-3072, `SECURE_BOOT_SIGNING_KEY` | A 4 KB signature sector appended to the image | The bootloader              | Every boot, before the app runs  |
| OTA (Ed25519) | `FIRMWARE_SIGNING_PRIVATE_KEY`      | The `.sig` sidecar                            | The app, as the OTA streams | Before the slot becomes bootable |

The order is forced. Secure boot signing comes first, because it changes the
image: it pads it to a 4 KB boundary and appends the signature sector. Ed25519
comes second, over the exact secure-boot-signed bytes, because the dash checks
it over the bytes it receives ([Firmware OTA](../architecture/firmware-ota.md)).
Done the other way round, the `.sig` covers fewer bytes than the upload, and the
dash answers `ota_bad_signature`. The `secure (esp32s3)` CI job and the release
job both produce them in this order.

The app does not check the secure boot signature itself. The prebuilt OTA code
has no secure boot support. An image with a valid `.sig` but no secure boot
signature (the plain `canshift-esp32s3-*-firmware.bin`, for example) is
therefore written and committed. On the next boot the bootloader refuses it and
falls back to the slot that was running, so the dash comes back on its old
version. That is safe, but silent: send a fused dash the `-secure-` image.

## Bring-up on a sacrificial board

Run this only after the v0.20.2 bench has passed on the board (#10), and only
on a board you can afford to lose.

### What each step costs

| Step                                                  | Reversible?                                                                      |
| ----------------------------------------------------- | -------------------------------------------------------------------------------- |
| 1. Generate the signing key                           | **REVERSIBLE** — nothing touches a chip                                          |
| 2. Store and back it up                               | **REVERSIBLE**                                                                   |
| 3. Build and sign the images                          | **REVERSIBLE**                                                                   |
| 4. Pre-flight the chip                                | **REVERSIBLE** — read-only                                                       |
| 5. Pre-burn a host flash encryption key (recommended) | **IRREVERSIBLE** — burns one key block, but the board still boots plain firmware |
| 6. Flash the signed images                            | **REVERSIBLE until the chip boots them**                                         |
| 7. First boot                                         | **IRREVERSIBLE** — the bootloader burns every security eFuse                     |
| 8. Verify                                             | **REVERSIBLE**                                                                   |
| 9. Later updates                                      | **REVERSIBLE**                                                                   |

### Tools

On the machine that holds the key and on the bench machine:

```sh
python3 -m venv ~/canshift-secure-venv
~/canshift-secure-venv/bin/pip install "esptool==5.5.0" "bitstring<5" "cryptography==50.0.2"
export PATH="$HOME/canshift-secure-venv/bin:$PATH"
```

`bitstring<5` is needed: `espefuse` from esptool 5.5.0 fails to import with
bitstring 5. The bench machine also needs PlatformIO and Docker (`colima start`
on macOS), for the bootloader build.

### 1. Generate the signing key — REVERSIBLE

Do this on a machine you trust, offline if you can, and write the key straight
onto an encrypted removable volume (an encrypted APFS or VeraCrypt volume). It
must never touch the repository, a cloud-synced folder or a shell history file.

```sh
KEYVOL=/Volumes/canshift-keys
umask 077
espsecure generate-signing-key --version 2 --scheme rsa3072 "$KEYVOL/secure-boot-signing-key.pem"
espsecure extract-public-key --version 2 --keyfile "$KEYVOL/secure-boot-signing-key.pem" "$KEYVOL/secure-boot-public.pem"
espsecure digest-sbv2-public-key --keyfile "$KEYVOL/secure-boot-public.pem" --output "$KEYVOL/secure-boot-key-digest.bin"
od -An -tx1 -v "$KEYVOL/secure-boot-key-digest.bin" | tr -d ' \n'; echo
```

The last line prints the key digest, 64 hex characters. Write it down. It is
what the chip gets fused with, and it is not secret.

### 2. Store and back it up — REVERSIBLE

| What                                                | Secret?           | Where                                                                                                                                      |
| --------------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `secure-boot-signing-key.pem`                       | **Yes**           | The encrypted volume (primary)                                                                                                             |
| Backup of the key                                   | **Yes**           | `age -p -o secure-boot-signing-key.pem.age secure-boot-signing-key.pem` onto a second medium, stored in a different place from the primary |
| The backup passphrase                               | **Yes**           | On paper, somewhere other than the backup                                                                                                  |
| Key digest                                          | No                | The bench log, and `SECURE_BOOT_KEY_DIGEST` if the release job is ever enabled                                                             |
| `secure-boot-public.pem`                            | No                | Next to the digest. Lets anyone check a signed image                                                                                       |
| Per-chip flash encryption key (step 5)              | **Yes**, per chip | The encrypted volume, named after the chip's MAC. Losing it loses serial recovery for that chip only                                       |
| `espefuse summary` taken before first boot (step 4) | No                | The bench log. It cannot be read again after first boot                                                                                    |

Restore the backup on another machine and check that it prints the same digest
**before** you fuse anything. A key you cannot restore is a lost key, and a lost
key freezes every chip fused with it.

### 3. Build and sign the images — REVERSIBLE

From the firmware repo, on the bench machine:

```sh
pio run -e waveshare_s3_28_secure
pio run -e waveshare_s3_28_secure -t buildfs
scripts/secure_boot_bootloader.sh esp32s3 .pio/secure-boot/unsigned
scripts/secure_boot_sign.sh "$KEYVOL/secure-boot-signing-key.pem" \
  .pio/secure-boot/unsigned/bootloader.bin \
  .pio/build/waveshare_s3_28_secure/firmware.bin \
  .pio/secure-boot/signed
```

Mount the key volume only for the signing command, and eject it straight
after. Check that the `secure boot key digest:` line is the digest from step 1.
If it is not, stop: the images are signed by a different key.

### 4. Pre-flight the chip — REVERSIBLE

Connect the board over USB.

```sh
PORT=/dev/cu.usbmodem101
esptool --chip esp32s3 -p "$PORT" read-mac
esptool --chip esp32s3 -p "$PORT" get-security-info
espefuse --chip esp32s3 -p "$PORT" summary > "efuse-summary-before.txt"
```

Go on only if `get-security-info` reports secure boot and flash encryption
disabled, `SPI_BOOT_CRYPT_CNT` is `0`, and every `KEY_PURPOSE_*` is `USER`. Keep the
summary in the bench log, outside the repository. This is the last time the
chip's eFuses can be read.

### 5. Pre-burn a host flash encryption key — IRREVERSIBLE, recommended here

> [!CAUTION]
> **IRREVERSIBLE.** This burns key block `BLOCK_KEY0` and read-protects it. The
> board still boots its normal firmware afterwards, because flash encryption
> only starts at step 7, but that key block is spent for good.

If you skip this step, the bootloader generates the flash encryption key inside
the chip at step 7 and nobody ever knows it. Then the only way to update the
board is OTA, and a board whose app does not boot is lost. With a key generated
on the host, you can pre-encrypt an image and write it over USB, so recovery
stays possible ([Recovery](#recovery)). For a sacrificial board, that is the
point of the exercise.

```sh
MAC=<mac from step 4, without colons>
espsecure generate-flash-encryption-key --keylen 256 "$KEYVOL/flash-encryption-$MAC.bin"
espefuse --chip esp32s3 -p "$PORT" burn-key BLOCK_KEY0 "$KEYVOL/flash-encryption-$MAC.bin" XTS_AES_128_KEY
```

`espefuse` asks you to type `BURN`. You can rehearse it first against virtual
eFuses, with no chip involved:
`espefuse --chip esp32s3 --virt burn-key BLOCK_KEY0 <key> XTS_AES_128_KEY`.

### 6. Flash the signed images — REVERSIBLE until the chip boots them

```sh
esptool --chip esp32s3 -p "$PORT" erase-flash
esptool --chip esp32s3 -p "$PORT" --after no-reset write-flash \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x0 .pio/secure-boot/signed/bootloader.bin \
  0x8000 .pio/build/waveshare_s3_28_secure/partitions.bin \
  0x10000 .pio/secure-boot/signed/firmware.bin \
  0x810000 .pio/build/waveshare_s3_28_secure/spiffs.bin
```

`keep` matters. If `esptool` rewrites the flash mode in the bootloader header,
it breaks the bootloader's signature, and the ROM refuses it from the second
boot onward. `--after no-reset` keeps the chip in download mode: nothing is
burned yet. Up to this point you can still flash the plain release image
instead and walk away.

### 7. First boot — IRREVERSIBLE

> [!CAUTION]
> **IRREVERSIBLE, and the point of no return.** The moment the chip boots the
> secure bootloader, it burns the key digest and every security eFuse. **Do not
> cut power** until the dash shows its splash: the bootloader encrypts the
> flash in place, and an interrupted pass leaves the flash half encrypted.

Unplug the USB cable and plug it back in. On that first boot the bootloader, in
this order:

1. burns the key digest from its own signature block into a key block
   (`SECURE_BOOT_DIGEST0`), and **revokes the two unused digest slots**. No
   second key can ever be added;
2. burns `SECURE_BOOT_EN`, `DIS_DIRECT_BOOT`, `ENABLE_SECURITY_DOWNLOAD`,
   `HARD_DIS_JTAG`, `DIS_USB_JTAG` and `SOFT_DIS_JTAG`. From here on the ROM
   checks the bootloader on every boot, and `esptool` only works with
   `--no-stub`;
3. takes the key from step 5, or generates one inside the chip;
4. encrypts the bootloader, the partition table, the app in `app0`, otadata
   and coredump in place;
5. burns `SPI_BOOT_CRYPT_CNT` to `0b111` (release mode), plus
   `DIS_DOWNLOAD_MANUAL_ENCRYPT`, `DIS_DOWNLOAD_ICACHE`, `DIS_DOWNLOAD_DCACHE`
   and `WR_DIS_RD_DIS`, then resets;
6. verifies the app signature and starts the app.

The bootloader prints nothing ([why](#how-the-secure-build-is-put-together)).
Watch the dash, or `pio device monitor`, for the app's own boot.

`DIS_USB_JTAG` disables only the JTAG side of the S3's USB port. The USB serial
link the tuner uses keeps working.

### 8. Verify — REVERSIBLE

1. The dash boots to its pages and the tuner connects over USB.
2. Secure download mode now requires `--no-stub`:

   ```sh
   esptool --chip esp32s3 -p "$PORT" --no-stub get-security-info
   ```

   Expect `Secure Boot: Enabled`, `Flash Encryption: Enabled`,
   `SPI_BOOT_CRYPT_CNT` at `0x7`, key purposes `SECURE_BOOT_DIGEST0` and
   `XTS_AES_128_KEY`, and secure download mode on.

3. `esptool --chip esp32s3 -p "$PORT" --no-stub read-flash 0x10000 0x1000 probe.bin`
   must be refused.
4. Prove the bootloader enforces it. From the tuner (Device → Firmware update),
   upload the **plain** `canshift-esp32s3-vX.Y.Z-firmware.bin` and its `.sig`
   from a release older than the one running. The dash accepts it, restarts,
   and comes back **on the version it was running**: the bootloader refused the
   unsigned image. Then upload the matching `-secure-` image and check that the
   version changes.

### 9. Later updates — REVERSIBLE

- **App, over the air**: from the tuner, choose
  `canshift-esp32s3-secure-vX.Y.Z-firmware.bin` and its `.sig`. The dash checks
  the Ed25519 signature while the image streams, writes it to the spare slot
  (encrypted by the flash hardware as it is written), and restarts. The
  bootloader checks the secure boot signature. The new app marks itself valid
  after a stable boot, or the bootloader rolls back to the previous slot. The
  plain `canshift-esp32s3-vX.Y.Z-firmware.bin` is refused at boot, as in step 8.
- **SPIFFS, over USB**: SPIFFS is plaintext, so the release image writes as is:
  `esptool --chip esp32s3 -p "$PORT" --no-stub write-flash 0x810000 canshift-esp32s3-vX.Y.Z-spiffs.bin`.
- **The browser flasher** in the tuner (`/firmware`) writes a merged plain image.
  **Never use it on a fused dash.** It would overwrite the bootloader with an
  unsigned one, and the chip would never boot again.
- **The bootloader** cannot be updated: the app has no path to that region.
  A bootloader bug is permanent on every fused chip.

## Recovery

| What went wrong                             | When              | Way out                                                                                                                                                                                                                                                                              |
| ------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| A signature or digest check fails in step 3 | Before any chip   | Fix the inputs and sign again                                                                                                                                                                                                                                                        |
| Wrong images flashed in step 6              | Before first boot | Flash again. Nothing is burned until the chip boots                                                                                                                                                                                                                                  |
| Power cut during the first boot             | Step 7            | Cut during encryption, the ROM sees a ciphertext bootloader and stops. Write the step 6 images again with `--no-stub` (secure download mode is on by then) and boot: the bootloader skips what is already burned and encrypts again. If the ROM refuses the write, the board is lost |
| The app crashes on every boot after fusing  | After step 7      | With a step 5 key: pre-encrypt a working image and write it over USB (below). Without one: the board is lost                                                                                                                                                                         |
| A bad OTA                                   | Any time          | The bootloader refuses an unsigned image, and app rollback refuses one that crashes before marking itself valid. Both fall back to the running slot                                                                                                                                  |
| Signing key lost                            | Any time          | None. Fused dashes run their current image forever                                                                                                                                                                                                                                   |
| Signing key leaked                          | Any time          | None. The unused digest slots are revoked at first boot, so there is no second key to move to. Retire the boards                                                                                                                                                                     |

Writing a working app over USB to a chip with a step 5 key:

```sh
for SLOT in 0x10000 0x410000; do
  espsecure encrypt-flash-data --aes-xts --keyfile "$KEYVOL/flash-encryption-$MAC.bin" \
    --address "$SLOT" --output "firmware-$SLOT.enc" .pio/secure-boot/signed/firmware.bin
  esptool --chip esp32s3 -p "$PORT" --no-stub write-flash "$SLOT" "firmware-$SLOT.enc"
done
```

XTS encryption depends on the address, so each slot gets its own encrypted
copy. Both slots are written, so whichever one otadata points to boots. Run this
once on the sacrificial board on purpose: it is the recovery procedure #10 asks
to verify.

## Signing in CI

Two jobs sign secure images, and neither touches a chip.

- `secure (esp32s3)` in `build.yml` runs on every PR. It generates a throwaway
  RSA-3072 key in `$RUNNER_TEMP` and signs with it. It checks that the images
  refuse a second key, and that the Ed25519 signature wraps the secure image.
  Then it deletes the keys and the images, and uploads nothing. An image signed
  by a key that no longer exists must never reach a chip: the chip would accept
  that image and nothing after it.
- `secure-build` and `secure-sign` in `release.yml` sign the release with the
  real key and attach
  `canshift-esp32s3-secure-vX.Y.Z-{bootloader,partitions,firmware}.bin` plus
  their `.sig`. **They are off.** They run only when the repository variable
  `SECURE_BOOT_RELEASE` is `true`. `secure-build` compiles with no secret;
  `secure-sign` receives the unsigned images as an artifact, runs in the
  `secure-boot-signing` environment, installs only the hash-locked esptool, and
  fails unless the key it decodes matches `SECURE_BOOT_KEY_DIGEST`. The Ed25519
  `.sig` files come from `ota-sign`, like every other release image
  ([firmware signing](firmware-signing.md)). The secure assets stay out of
  `manifest.json`, so the tuner never offers them to a plain dash. There is no
  merged secure image:
  flashing one onto a virgin chip would fuse it on the next boot.

Putting the key in CI is a custody decision. Signing offline with
`secure_boot_sign.sh` keeps the key on the encrypted volume. Signing in CI
trades that for a second copy held by GitHub, behind an environment approval.
`espsecure sign-data` also supports a PKCS#11 HSM (`--hsm --hsm-config`), and
external signing (`--pub-key` with `--signature`), if the key should move onto
a hardware token later.

To enable the release jobs, once the bench has passed. First, in **Settings →
Environments**, create `secure-boot-signing`, with yourself as a required
reviewer and **Deployment branches and tags** limited to `main` and the `v*`
tags. Then:

```sh
REPO=CANShift/canshift-firmware
base64 -i "$KEYVOL/secure-boot-signing-key.pem" | gh secret set SECURE_BOOT_SIGNING_KEY --env secure-boot-signing -R "$REPO"
gh variable set SECURE_BOOT_KEY_DIGEST --env secure-boot-signing -R "$REPO" --body "<digest from step 1>"
gh variable set SECURE_BOOT_RELEASE -R "$REPO" --body true
```

Create the environment before you set the variable. A job that names an
environment that does not exist creates it, without any protection.

## Known gaps

- **No anti-rollback.** The bootloader's anti-rollback needs a secure version
  compiled into the app, and the prebuilt app does not carry one. Both
  signatures accept an older signed image, so a downgrade to a vulnerable
  signed release stays possible.
- **NVS and SPIFFS are plaintext** on flash ([why](#what-it-protects-and-what-it-does-not)).
- **One key per chip, for life.** The bootloader revokes the unused digest
  slots at first boot. Signing the bootloader with up to three keys before the
  first boot would leave room to rotate. That is a production decision, not a
  bench one.
- **The bootloader is silent and permanent** (log level `NONE`, no update path).
- Each of these needs the app built from ESP-IDF sources (`framework =
arduino, espidf`), with secure boot compiled into the app. That is a separate
  piece of work.

## The CrowPanel `[env:secure]`

`[env:secure]` (ESP32, `ota_4mb_secure.csv`, `sdkconfig.defaults.secure`) does
**not** produce a secure boot image. Its `sdkconfig` overrides never reach the
prebuilt Arduino framework, so its `bootloader.bin` is the stock, unsigned one,
and its partition table flags `nvs` and `spiffs`, which cannot be encrypted.
Never use it to fuse a chip (#371). The CrowPanel board is retired, so it has
not been redone. The S3 approach above carries over: a `secure-boot/esp32/`
project, with the bootloader at `0x1000` and a `0x7000` budget.
