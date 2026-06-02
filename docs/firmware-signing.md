# Firmware release signing (Ed25519)

The release workflow (`.github/workflows/release.yml`) signs every shipped
firmware binary with an Ed25519 private key whose public counterpart is
embedded in the [`canshift-flasher`](https://github.com/tburkhalterr/canshift-flasher)
build via the `VITE_FIRMWARE_PUBLIC_KEY` env. The flasher verifies the
detached `.sig` sidecar before calling `writeFlash`, defending against a
hosting-origin compromise that swaps both the binary and its SHA-256
sidecar from the same source. See issue
[#1259](https://github.com/tburkhalterr/CANShift/issues/1259).

## Released assets

For every release `v<x.y.z>`, the workflow uploads:

- `canshift-firmware-v<x.y.z>-crowpanel_28-merged.bin` (USB factory image)
- `canshift-firmware-v<x.y.z>-crowpanel_28-firmware.bin` (OTA payload)
- `canshift-spiffs-v<x.y.z>-crowpanel_28.bin` (SPIFFS data partition)

Each of the three ships alongside a `.sig` sidecar (binary, 64 bytes) when
the signing key is configured.

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

- Exits non-zero if the secret is set but malformed.
- Exits `0` with a warning when the secret is missing — the release still ships, just without `.sig` files. Keeps the workflow green during the rollout window while the flasher's verification stays no-op (per canshift-flasher#61).

## Distributing the public key to the flasher

Provide the **public** PEM (`firmware-signing-public.pem`) to the flasher build pipeline. The current contract is single-key, embedded at build time via `VITE_FIRMWARE_PUBLIC_KEY` (raw PEM, not base64).

## Rotation runbook

The simplest model is **single-key, manual rotation** — minimises operational complexity at the cost of a flasher rebuild + redeploy for any rotation.

1. Generate a new keypair via the steps above.
2. Update `VITE_FIRMWARE_PUBLIC_KEY` in the flasher repo and ship a new flasher build. Users automatically pick it up on the next visit to `canshift.tmbk.ch`.
3. Once telemetry confirms the new flasher build has reached its install base (≥ 99 % visitors over a 7-day window), rotate `FIRMWARE_SIGNING_PRIVATE_KEY` in this repo's CI secret to the new private key.
4. Releases shipped between step 2 and step 3 will still verify against the old key. The flasher PR that introduces verification (canshift-flasher#61) keeps the old key around as a transition fallback for one release cycle.

For an emergency rotation (private key suspected to be exposed), skip the gradual rollout: rotate the CI secret immediately, force-push a new flasher build, accept the user friction.

A multi-key variant (flasher embeds a key set, verifies against any one of N) is a future option — see issue #1259 for the discussion.

## References

- Issue [#1259](https://github.com/tburkhalterr/CANShift/issues/1259) — this feature.
- [canshift-flasher#61](https://github.com/tburkhalterr/canshift-flasher/issues/61) — flasher-side verification.
- `.github/workflows/release.yml` — CI step.
- `scripts/sign_release_artifacts.py` — signing helper.
