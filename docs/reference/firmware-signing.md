# Firmware release signing

Source: [`scripts/sign_release_artifacts.py`](../../scripts/sign_release_artifacts.py)

The release workflow (`.github/workflows/release.yml`) signs every shipped
firmware binary with an Ed25519 private key. **The dash enforces it.** The
public key, `keys/firmware-signing-public.pem`, is compiled into the firmware,
and an OTA image is only made bootable once it verifies against that key, over
USB or the workshop socket ([Firmware OTA](../architecture/firmware-ota.md)).
An image without a signature is refused.

A host may check the `.sig` too before it uploads, to fail early, but the dash
does not rely on it.

## Released assets

For every release `v<x.y.z>`, the workflow uploads three images per chip family
(`esp32`, `esp32s3`):

- `canshift-<chip>-v<x.y.z>-merged.bin` (USB factory image, see [First flash](../guide/first-flash.md))
- `canshift-<chip>-v<x.y.z>-firmware.bin` (OTA payload)
- `canshift-<chip>-v<x.y.z>-spiffs.bin` (SPIFFS data partition)

Each of the three ships alongside a `.sig` sidecar (binary, 64 bytes) when
the signing key is configured.

## Secure boot images

A secure boot release adds `canshift-esp32s3-secure-v<x.y.z>-{bootloader,partitions,firmware}.bin`,
each with a `.sig`. Those images already carry a secure boot v2 signature. The
Ed25519 `.sig` is computed over the secure-boot-signed bytes, never before
secure boot signing, because the dash checks it over exactly what it receives.
See [Secure boot setup](secure-boot-setup.md#two-signatures-in-a-fixed-order).

## Generating the keypair

Run once on a trusted machine (offline if possible). OpenSSL 3.x is enough:

```sh
# Generate the private key in PKCS8 PEM
openssl genpkey -algorithm ed25519 -out firmware-signing-private.pem

# Extract the public key
openssl pkey -in firmware-signing-private.pem -pubout -out firmware-signing-public.pem

# Base64-encode the private key for the GitHub Actions secret
base64 -i firmware-signing-private.pem
```

## Configuring CI

1. In the repo's **Settings → Secrets and variables → Actions → New repository secret**, add a secret named `FIRMWARE_SIGNING_PRIVATE_KEY` and paste the base64 blob from the previous step.
2. Restrict the workflow that consumes it (see `.github/workflows/release.yml`) — already pinned to `branches: [main]` plus `permissions: { contents: write }`.

The release workflow's `Sign firmware artifacts with Ed25519` step:

- Fails when the secret is missing or malformed. A release without `.sig`
  files could not be installed over the air.
- Verifies every `.sig` it just wrote against `keys/firmware-signing-public.pem`
  (`scripts/verify_release_signatures.py`), so a secret that does not match the
  key the firmware embeds fails the release instead of shipping images every
  dash would refuse.

## Where the public key lives

`keys/firmware-signing-public.pem` is the only copy. `rust/ota-signature/build.rs`
reads it at build time and checks it is an Ed25519 SubjectPublicKeyInfo. Hosts
that want to check signatures too read the same file. The private key never
enters the repo.

## Rotation runbook

A dash only accepts images signed by the key it was built with, so a rotation
has to travel inside a release signed by the old key.

1. Generate a new keypair via the steps above.
2. Replace `keys/firmware-signing-public.pem` with the new public key and
   release. **Sign that release with the old key.** Dashes accept it because
   it verifies against the key they run, and from then on they accept only the
   new one.
3. Rotate `FIRMWARE_SIGNING_PRIVATE_KEY` to the new private key. Every later
   release is signed with it.

A dash that skipped step 2's release cannot take a release from step 3 over
the air. It needs the step 2 release first, or a cable and `esptool`
([First flash](../guide/first-flash.md)).

For an emergency rotation (private key suspected to be exposed), the old key
can no longer be trusted to carry the new one. Rotate both immediately, and
reflash dashes over USB.

## References

- Issue [#1259](https://github.com/CANShift/issues/1259) — this feature.
- `.github/workflows/release.yml` — CI step.
- `scripts/sign_release_artifacts.py` — signing helper.
