#!/usr/bin/env bash
# secure_boot_first_flash.sh — one-time secure-boot v2 + flash-encryption
# provisioning for a CANShift production unit. Burns one-way eFuses and
# writes the signed/encrypted image. See docs/secure-boot-setup.md.
#
# THIS IS IRREVERSIBLE. DO NOT RUN ON DEV BOARDS.
#
# Usage:
#   ./scripts/secure_boot_first_flash.sh --i-understand-this-is-irreversible
#
# Environment overrides:
#   DEV_BOARD_DO_NOT_RUN=1   → script refuses to run (set on dev workstations)
#   ESP_PORT=/dev/cu.usb...  → override auto-detected serial port

set -euo pipefail

# ---------------------------------------------------------------------------
# Hard guards — these are deliberately the very first thing the script does.
# Any change to fuse-burning logic must keep these checks at the top.
# ---------------------------------------------------------------------------

if [[ "${DEV_BOARD_DO_NOT_RUN:-0}" == "1" ]]; then
    echo "Refusing: DEV_BOARD_DO_NOT_RUN=1 is set in the environment." >&2
    echo "This is a dev workstation. Run on the production-flash workstation only." >&2
    exit 1
fi

if [[ "${1:-}" != "--i-understand-this-is-irreversible" ]]; then
    cat <<'EOF' >&2
================================================================================
secure_boot_first_flash.sh — one-time per-chip secure-boot provisioning
================================================================================
This script burns one-way eFuses on the connected ESP32. Wrong settings
BRICK the chip permanently. There is no recovery — replace the chip.

DO NOT RUN ON A DEVELOPMENT BOARD.

Read docs/secure-boot-setup.md end to end before invoking. Re-run with the
flag below if you have read it and you understand the consequences:

    ./scripts/secure_boot_first_flash.sh --i-understand-this-is-irreversible

================================================================================
EOF
    exit 1
fi

# ---------------------------------------------------------------------------
# Resolve toolchain paths and project layout.
# ---------------------------------------------------------------------------

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FIRMWARE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly SECRETS_DIR="${FIRMWARE_DIR}/secrets"
readonly KEYS_DIR="${FIRMWARE_DIR}/keys"
readonly ESCROW_CSV="${KEYS_DIR}/escrow.csv"
readonly SIGNING_KEY="${SECRETS_DIR}/secure_boot_signing_key.pem"

readonly ESPTOOL_PKG="${HOME}/.platformio/packages/tool-esptoolpy"
readonly ESPTOOL_PY="${ESPTOOL_PKG}/esptool.py"
readonly ESPSECURE_PY="${ESPTOOL_PKG}/espsecure.py"
readonly ESPEFUSE_PY="${ESPTOOL_PKG}/espefuse.py"

for tool in "${ESPTOOL_PY}" "${ESPSECURE_PY}" "${ESPEFUSE_PY}"; do
    if [[ ! -f "${tool}" ]]; then
        echo "Missing toolchain: ${tool}" >&2
        echo "Install PlatformIO Core and run 'pio run -e crowpanel_28' once" >&2
        echo "to populate the Espressif toolchain cache." >&2
        exit 1
    fi
done

readonly ESP_PORT="${ESP_PORT:-$(ls /dev/cu.usbserial-* /dev/cu.SLAB_* /dev/cu.wchusbserial* 2>/dev/null | head -n 1 || true)}"
if [[ -z "${ESP_PORT}" ]]; then
    echo "No serial port found. Connect the device and re-run, or set ESP_PORT=." >&2
    exit 1
fi

readonly OPERATOR_EMAIL="$(git config user.email 2>/dev/null || echo 'unknown')"

mkdir -p "${KEYS_DIR}"
if [[ ! -f "${ESCROW_CSV}" ]]; then
    echo "chip_mac,flash_key_sha256,timestamp_utc,operator,post_mortem" > "${ESCROW_CSV}"
fi

# ---------------------------------------------------------------------------
# Step 0 — 60-second hard prompt + chip confirmation.
# ---------------------------------------------------------------------------

cat <<EOF
================================================================================
About to provision secure boot v2 + flash encryption on the device at:
  port:           ${ESP_PORT}
  operator:       ${OPERATOR_EMAIL}
  signing key:    ${SIGNING_KEY}
  per-chip keys:  ${KEYS_DIR}/

This is IRREVERSIBLE. 60 seconds to abort with Ctrl-C.
================================================================================
EOF

for i in 60 55 50 45 40 35 30 25 20 15 10 5; do
    printf "  %2d s remaining…\n" "${i}"
    sleep 5
done

echo "Reading chip identity…"
readonly CHIP_MAC="$("${ESPTOOL_PY}" --port "${ESP_PORT}" read_mac \
    | awk '/MAC:/ {print $2; exit}')"
if [[ -z "${CHIP_MAC}" ]]; then
    echo "Could not read chip MAC. Check cable + port." >&2
    exit 1
fi
echo "Chip MAC: ${CHIP_MAC}"

read -r -p "Type the chip MAC to confirm provisioning of THIS chip: " CONFIRM_MAC
if [[ "${CONFIRM_MAC}" != "${CHIP_MAC}" ]]; then
    echo "MAC mismatch. Aborting." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Pre-flight — refuse if the chip already has any secure-boot fuse burned.
# A partially-fused chip is treated as already-bricked.
# ---------------------------------------------------------------------------

echo "Pre-flight: checking eFuse summary…"
readonly EFUSE_SUMMARY="$("${ESPEFUSE_PY}" --port "${ESP_PORT}" summary --format value-only \
    ABS_DONE_1 FLASH_CRYPT_CNT BLOCK1 BLOCK2 2>/dev/null)"

if echo "${EFUSE_SUMMARY}" | grep -qE '[1-9a-fA-F]'; then
    echo "Refusing: chip already has secure-boot or flash-encryption fuses set." >&2
    echo "Output of efuse summary (ABS_DONE_1 FLASH_CRYPT_CNT BLOCK1 BLOCK2):" >&2
    echo "${EFUSE_SUMMARY}" >&2
    echo "If this is a re-run after an interrupt, RESUME from the last completed step manually." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Per-chip flash key — refuse to overwrite an existing one.
# ---------------------------------------------------------------------------

readonly FLASH_KEY_FILE="${KEYS_DIR}/flash_${CHIP_MAC//:/}.bin"
if [[ -f "${FLASH_KEY_FILE}" ]]; then
    echo "Refusing: per-chip flash key already exists at ${FLASH_KEY_FILE}." >&2
    echo "Move it offline (or delete it after offline backup) before re-running." >&2
    exit 1
fi

echo "Generating per-chip AES-XTS-256 flash encryption key…"
"${ESPSECURE_PY}" generate_flash_encryption_key "${FLASH_KEY_FILE}"

readonly FLASH_KEY_SHA256="$(shasum -a 256 "${FLASH_KEY_FILE}" | awk '{print $1}')"
echo "Flash key SHA-256: ${FLASH_KEY_SHA256}"

# ---------------------------------------------------------------------------
# Verify signing key.
# ---------------------------------------------------------------------------

if [[ ! -f "${SIGNING_KEY}" ]]; then
    echo "Refusing: secure-boot signing key not found at ${SIGNING_KEY}." >&2
    echo "Generate it once per project via:" >&2
    echo "    ./scripts/generate_keys.sh --i-understand-this-is-irreversible" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Build the signed/encryptable image.
# ---------------------------------------------------------------------------

echo "Building [env:secure] firmware…"
( cd "${FIRMWARE_DIR}" && pio run -e secure -t buildprog )

readonly BUILD_DIR="${FIRMWARE_DIR}/.pio/build/secure"
readonly BOOTLOADER_BIN="${BUILD_DIR}/bootloader.bin"
readonly PARTITIONS_BIN="${BUILD_DIR}/partitions.bin"
readonly FIRMWARE_BIN="${BUILD_DIR}/firmware.bin"
readonly SPIFFS_BIN="${BUILD_DIR}/spiffs.bin"

for art in "${BOOTLOADER_BIN}" "${PARTITIONS_BIN}" "${FIRMWARE_BIN}"; do
    if [[ ! -f "${art}" ]]; then
        echo "Build artifact missing: ${art}" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# Write flash. Bootloader + partition table go plaintext (signed, not
# encrypted). Everything else is written --encrypt so the chip applies
# AES-XTS on its way to the SPI flash.
# ---------------------------------------------------------------------------

echo "Flashing bootloader + partition table (plaintext, signed)…"
"${ESPTOOL_PY}" --port "${ESP_PORT}" --baud 460800 write_flash \
    0x1000  "${BOOTLOADER_BIN}" \
    0x8000  "${PARTITIONS_BIN}"

echo "Flashing app + filesystem (encrypted)…"
"${ESPTOOL_PY}" --port "${ESP_PORT}" --baud 460800 write_flash --encrypt \
    0x10000 "${FIRMWARE_BIN}"

if [[ -f "${SPIFFS_BIN}" ]]; then
    "${ESPTOOL_PY}" --port "${ESP_PORT}" --baud 460800 write_flash --encrypt \
        0x310000 "${SPIFFS_BIN}"
fi

# ---------------------------------------------------------------------------
# Burn fuses — operator-confirmed pauses between steps 2/3 and 4/5.
# ---------------------------------------------------------------------------

confirm_step() {
    local message="$1"
    echo
    read -r -p "${message} — type 'continue' to proceed (anything else aborts): " ans
    if [[ "${ans}" != "continue" ]]; then
        echo "Aborted by operator." >&2
        exit 1
    fi
}

echo
echo "===== Step 1/7 — burn BLOCK2 with SHA-256 of secure-boot pubkey ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_key_digest "${SIGNING_KEY}"

echo
echo "===== Step 2/7 — burn ABS_DONE_1 (activate secure boot v2 enforcement) ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse ABS_DONE_1

confirm_step "Secure boot enforcement is now ON. Verify before continuing"

echo
echo "===== Step 3/7 — burn BLOCK1 with per-chip AES-256 flash-encryption key ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_key flash_encryption "${FLASH_KEY_FILE}"

echo
echo "===== Step 4/7 — burn FLASH_CRYPT_CNT to 0x7F (release mode) ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse FLASH_CRYPT_CNT 0x7F

confirm_step "Flash encryption is now in release mode. Verify before continuing"

echo
echo "===== Step 5/7 — close UART download backdoors ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse DIS_DOWNLOAD_DCACHE 1
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse DIS_DOWNLOAD_ICACHE 1

echo
echo "===== Step 6/7 — disable JTAG ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse JTAG_DISABLE 1

echo
echo "===== Step 7/7 — disable USB-JTAG (no-op on original ESP32) ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" --do-not-confirm \
    burn_efuse DIS_USB_JTAG 1 || \
    echo "DIS_USB_JTAG not present on this chip — skipping (expected on ESP32)."

# ---------------------------------------------------------------------------
# Append escrow row.
# ---------------------------------------------------------------------------

readonly TS_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "${CHIP_MAC},${FLASH_KEY_SHA256},${TS_UTC},${OPERATOR_EMAIL}," >> "${ESCROW_CSV}"

# ---------------------------------------------------------------------------
# Final verification.
# ---------------------------------------------------------------------------

echo
echo "===== Final verification ====="
"${ESPEFUSE_PY}" --port "${ESP_PORT}" summary

cat <<EOF

================================================================================
Provisioning complete for chip ${CHIP_MAC}.

  - Power-cycle the device and capture the boot log.
  - Confirm the boot log reaches app_main and shows the secure-boot v2
    banner from the ROM bootloader.
  - Move ${FLASH_KEY_FILE} to OFFLINE storage before flashing the next chip.
  - Verify ${ESCROW_CSV} now contains the new row.

If anything looks wrong, mark the post_mortem column in escrow.csv and
treat the chip as e-waste — DO NOT ship a unit with an indeterminate state.
================================================================================
EOF
