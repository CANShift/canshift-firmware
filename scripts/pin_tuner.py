import os
import sys
import urllib.error
import urllib.request

from tuner_bundle import ASSET_NAME, PIN_FILE, RELEASE_URL, SHA256_PATTERN, TAG_PATTERN

CHECKSUM_SUFFIX = ".sha256"
TIMEOUT_S = 30
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fetch_checksum(tag):
    asset = ASSET_NAME.format(tag=tag) + CHECKSUM_SUFFIX
    try:
        with urllib.request.urlopen(
            RELEASE_URL.format(tag=tag, asset=asset), timeout=TIMEOUT_S
        ) as response:
            fields = response.read().decode("ascii", errors="replace").split()
    except urllib.error.URLError as err:
        sys.stderr.write(f"{tag}: {asset} could not be fetched: {err}\n")
        return ""
    return fields[0].lower() if fields else ""


def main(argv):
    if len(argv) != 2 or not TAG_PATTERN.match(argv[1]):
        sys.stderr.write("usage: python3 scripts/pin_tuner.py vX.Y.Z\n")
        return 2
    tag = argv[1]
    sha256 = fetch_checksum(tag)
    if not SHA256_PATTERN.match(sha256):
        sys.stderr.write(f"{tag}: checksum asset does not hold a sha256\n")
        return 1
    with open(os.path.join(ROOT, PIN_FILE), "w", encoding="utf-8") as handle:
        handle.write(f"tag={tag}\nsha256={sha256}\n")
    print(f"pinned tuner {tag} ({sha256})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
