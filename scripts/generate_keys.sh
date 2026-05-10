#!/usr/bin/env bash
# generate_keys.sh — one-time-per-project secure-boot signing key generation.
# Produces secrets/secure_boot_signing_key.pem (RSA-3072) used by [env:secure].
# See docs/secure-boot-setup.md.
#
# THIS GENERATES THE MOST SECURITY-CRITICAL ARTIFACT IN THE PROJECT.
# Lose it → every fielded device is frozen on its current image.
# Leak it → an attacker can sign images that every device will accept.
#
# Usage:
#   ./scripts/generate_keys.sh --i-understand-this-is-irreversible
#
# Environment overrides:
#   DEV_BOARD_DO_NOT_RUN=1   → script refuses to run

set -euo pipefail

if [[ "${DEV_BOARD_DO_NOT_RUN:-0}" == "1" ]]; then
    echo "Refusing: DEV_BOARD_DO_NOT_RUN=1 is set in the environment." >&2
    exit 1
fi

if [[ "${1:-}" != "--i-understand-this-is-irreversible" ]]; then
    cat <<'EOF' >&2
================================================================================
generate_keys.sh — generate the project-wide secure-boot signing key
================================================================================
This is run ONCE per project, not per chip. The generated key signs every
firmware image that every CANShift production device will ever accept.

Custody requirements (read docs/secure-boot-setup.md section 6 first):
  - HSM / YubiKey, OR
  - Encrypted disk on a dedicated, network-isolated workstation, AND
  - Encrypted offline backup, AND
  - Two-person control if at all possible.

DO NOT commit the key. DO NOT put the key in CI. DO NOT store the key in
a cloud-synced folder. .gitignore blocks secrets/, keys/, and *.pem
defensively, but the discipline is yours.

Re-run with the flag below if you understand:
    ./scripts/generate_keys.sh --i-understand-this-is-irreversible
================================================================================
EOF
    exit 1
fi

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIRMWARE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SECRETS_DIR="${FIRMWARE_DIR}/secrets"
readonly SIGNING_KEY="${SECRETS_DIR}/secure_boot_signing_key.pem"
readonly GITIGNORE="${FIRMWARE_DIR}/.gitignore"

readonly ESPSECURE_PY="${HOME}/.platformio/packages/tool-esptoolpy/espsecure.py"
if [[ ! -f "${ESPSECURE_PY}" ]]; then
    echo "Missing toolchain: ${ESPSECURE_PY}" >&2
    echo "Install PlatformIO Core and run 'pio run -e crowpanel_28' once first." >&2
    exit 1
fi

mkdir -p "${SECRETS_DIR}"
chmod 700 "${SECRETS_DIR}"

if [[ -f "${SIGNING_KEY}" ]]; then
    echo "Refusing: signing key already exists at ${SIGNING_KEY}." >&2
    echo "If you intentionally want to rotate the key, move the existing one" >&2
    echo "out of the way first AND understand that fielded devices signed by" >&2
    echo "the old key will not accept images signed by the new one." >&2
    exit 1
fi

# Defensive idempotent .gitignore touch — adds the secrets/keys/pem block
# only if it is not already covered.
if ! grep -qE '^secrets/$' "${GITIGNORE}" 2>/dev/null; then
    {
        echo ""
        echo "# Secure-boot artifacts — NEVER commit."
        echo "secrets/"
        echo "keys/"
        echo "*.pem"
    } >> "${GITIGNORE}"
    echo "Updated ${GITIGNORE} with secure-boot ignore patterns."
fi

echo "Generating RSA-3072 secure-boot v2 signing key…"
"${ESPSECURE_PY}" generate_signing_key --version 2 "${SIGNING_KEY}"
chmod 600 "${SIGNING_KEY}"

cat <<EOF

================================================================================
Key generated at: ${SIGNING_KEY}

Next steps (do them NOW, not later):
  1. Verify the key is gitignored:   git check-ignore "${SIGNING_KEY}"
  2. Make an encrypted offline backup. Suggested options:
        - YubiKey PIV slot 9c (re-import via openssl + ykman)
        - age-encrypted blob on a USB stick stored in a safe
        - KeePassXC database on offline media
  3. Confirm the backup is recoverable on a SECOND machine before
     trusting the primary.
  4. Document custody in your project's security log (who, where, when).

If you cannot do steps 2-4 today, DELETE this key and re-run when you can.
================================================================================
EOF
