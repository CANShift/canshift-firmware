#!/usr/bin/env bash
set -euo pipefail

readonly IDF_IMAGE="espressif/idf:v4.4.6@sha256:3e7819d54174c932871e2f3adc64d189af214ca139458e073b78223762407944"

usage() {
    echo "usage: secure_boot_bootloader.sh <chip> <out-dir>" >&2
    exit 2
}

main() {
    [[ $# -eq 2 ]] || usage
    local chip=$1 out=$2 root project
    root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
    project="$root/secure-boot/$chip"
    if [[ ! -f "$project/sdkconfig.defaults" ]]; then
        echo "::error title=secure boot::no secure bootloader project for '$chip' under secure-boot/" >&2
        exit 1
    fi
    mkdir -p "$out"
    out=$(cd "$out" && pwd)
    docker run --rm \
        -v "$project:/project:ro" \
        -v "$out:/out" \
        -w /project \
        "$IDF_IMAGE" \
        bash -c 'idf.py -B /tmp/build -D SDKCONFIG=/tmp/build/sdkconfig bootloader \
            && cp /tmp/build/bootloader/bootloader.bin /out/bootloader.bin \
            && chmod a+r /out/bootloader.bin'
    echo "unsigned secure bootloader: $out/bootloader.bin"
}

main "$@"
