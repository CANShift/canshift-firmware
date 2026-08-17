# Firmware release signing

Source: [`scripts/sign_release_artifacts.py`](../../scripts/sign_release_artifacts.py)

The release workflow (`.github/workflows/release.yml`) signs every shipped
firmware binary with an Ed25519 private key. The public counterpart is
meant to be embedded in the Tuner's built-in USB flasher build via a Vite
env var, so the flasher can verify the detached `.sig` sidecar before
calling `writeFlash`, defending against a hosting-origin compromise that
swaps both the binary and its SHA-256 sidecar from the same source.
Flasher-side verification is not wired up yet. See issue
[#1259](https://github.com/CANShift/issues/1259).

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
- Exits `0` with a warning when the secret is missing — the release still ships, just without `.sig` files. Keeps the workflow green during the rollout window while the flasher's verification stays no-op (see #1259).

## Distributing the public key to the flasher

Provide the **public** PEM (`firmware-signing-public.pem`) to the Tuner build pipeline. The current contract is single-key, embedded at build time via a Vite env var (raw PEM, not base64).

## Rotation runbook

The simplest model is **single-key, manual rotation** — minimises operational complexity at the cost of a flasher rebuild + redeploy for any rotation.

1. Generate a new keypair via the steps above.
2. Update the embedded public key in the Tuner and ship a new build. Users automatically pick it up on their next visit.
3. Once telemetry confirms the new flasher build has reached its install base (≥ 99 % visitors over a 7-day window), rotate `FIRMWARE_SIGNING_PRIVATE_KEY` in this repo's CI secret to the new private key.
4. Releases shipped between step 2 and step 3 will still verify against the old key. The change that introduces flasher-side verification should keep the old key around as a transition fallback for one release cycle.

For an emergency rotation (private key suspected to be exposed), skip the gradual rollout: rotate the CI secret immediately, force-push a new flasher build, accept the user friction.

A multi-key variant (flasher embeds a key set, verifies against any one of N) is a future option — see issue #1259 for the discussion.

## References

- Issue [#1259](https://github.com/CANShift/issues/1259) — this feature.
- `.github/workflows/release.yml` — CI step.
- `scripts/sign_release_artifacts.py` — signing helper.
