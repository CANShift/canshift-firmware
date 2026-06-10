#!/usr/bin/env python3

from __future__ import annotations

import base64
import os
import sys
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import load_pem_private_key


def load_key() -> Ed25519PrivateKey:
    raw = os.environ.get("FIRMWARE_SIGNING_PRIVATE_KEY")
    if not raw:
        print("error: FIRMWARE_SIGNING_PRIVATE_KEY env var is empty", file=sys.stderr)
        sys.exit(1)
    try:
        pem_bytes = base64.b64decode(raw)
    except Exception as err:
        print(f"error: FIRMWARE_SIGNING_PRIVATE_KEY is not valid base64: {err}", file=sys.stderr)
        sys.exit(1)
    try:
        key = load_pem_private_key(pem_bytes, password=None)
    except Exception as err:
        print(f"error: PEM decode failed: {err}", file=sys.stderr)
        sys.exit(1)
    if not isinstance(key, Ed25519PrivateKey):
        print(
            f"error: expected an Ed25519 private key, got {type(key).__name__}",
            file=sys.stderr,
        )
        sys.exit(1)
    return key


def sign_one(key: Ed25519PrivateKey, path: Path) -> Path:
    if not path.is_file():
        print(f"error: {path} does not exist", file=sys.stderr)
        sys.exit(1)
    data = path.read_bytes()
    sig = key.sign(data)
    sig_path = path.with_suffix(path.suffix + ".sig")
    sig_path.write_bytes(sig)
    print(f"signed {path} -> {sig_path} ({len(sig)} bytes)")
    return sig_path


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: sign_release_artifacts.py <file> [<file> …]", file=sys.stderr)
        return 1
    key = load_key()
    for arg in sys.argv[1:]:
        sign_one(key, Path(arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
