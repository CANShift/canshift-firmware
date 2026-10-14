import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
from cryptography.hazmat.primitives.serialization import load_pem_public_key

SIG_SUFFIX = ".sig"


def load_key(path: Path) -> Ed25519PublicKey:
    key = load_pem_public_key(path.read_bytes())
    if not isinstance(key, Ed25519PublicKey):
        raise SystemExit(f"error: {path} is not an Ed25519 public key")
    return key


def verify(key: Ed25519PublicKey, image: Path) -> None:
    signature = image.with_suffix(image.suffix + SIG_SUFFIX)
    try:
        key.verify(signature.read_bytes(), image.read_bytes())
    except (InvalidSignature, OSError) as err:
        raise SystemExit(f"error: {image} does not verify against the embedded key: {err!r}")
    print(f"verified {image}")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: verify_release_signatures.py <public.pem> <file> [<file> …]", file=sys.stderr)
        return 1
    key = load_key(Path(sys.argv[1]))
    for arg in sys.argv[2:]:
        verify(key, Path(arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
