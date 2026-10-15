#!/usr/bin/env bash
set -euo pipefail

readonly PARTITION_TABLE_OFFSET=$((0x8000))
readonly ESPSECURE=(python3 -m espsecure)

usage() {
    echo "usage: secure_boot_sign.sh <signing-key.pem> <bootloader.bin> <firmware.bin> <out-dir>" >&2
    exit 2
}

sign_image() {
    local key=$1 image=$2 signed=$3
    "${ESPSECURE[@]}" sign-data --version 2 --keyfile "$key" --output "$signed" "$image"
}

verify_image() {
    local public_key=$1 signed=$2
    if ! "${ESPSECURE[@]}" verify-signature --version 2 --keyfile "$public_key" "$signed"; then
        echo "::error title=secure boot::$signed does not verify against its signing key" >&2
        exit 1
    fi
}

check_bootloader_fits() {
    local signed=$1 size
    size=$(($(wc -c < "$signed")))
    if ((size > PARTITION_TABLE_OFFSET)); then
        echo "::error title=secure boot::signed bootloader is $size bytes, past the partition table at $PARTITION_TABLE_OFFSET" >&2
        exit 1
    fi
    echo "signed bootloader: $size of $PARTITION_TABLE_OFFSET bytes"
}

print_key_digest() {
    local public_key=$1 digest_file=$2
    "${ESPSECURE[@]}" digest-sbv2-public-key --keyfile "$public_key" --output "$digest_file"
    echo "secure boot key digest: $(od -An -tx1 -v "$digest_file" | tr -d ' \n')"
}

main() {
    [[ $# -eq 4 ]] || usage
    local key=$1 bootloader=$2 firmware=$3 out=$4
    mkdir -p "$out"
    sign_image "$key" "$bootloader" "$out/bootloader.bin"
    sign_image "$key" "$firmware" "$out/firmware.bin"
    "${ESPSECURE[@]}" extract-public-key --version 2 --keyfile "$key" "$out/secure-boot-public.pem"
    verify_image "$out/secure-boot-public.pem" "$out/bootloader.bin"
    verify_image "$out/secure-boot-public.pem" "$out/firmware.bin"
    check_bootloader_fits "$out/bootloader.bin"
    print_key_digest "$out/secure-boot-public.pem" "$out/secure-boot-key-digest.bin"
}

main "$@"
