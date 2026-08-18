#!/usr/bin/env python3
"""Build the release manifest the tuner reads to decide what to flash.

    python3 scripts/build_manifest.py <tag> <version> [asset-dir] > manifest.json

A release publishes one universal firmware per chip family — the env flagged
`universal` in .github/boards.json — and the releasable boards as a catalog that
resolves to those firmwares through its `chip`. Checksums come from hashing the
staged assets; without an asset directory they are null, which is what the lint
smoke run uses. Run from the repository root.
"""

import hashlib
import json
import sys
from pathlib import Path

SCHEMA = 3
ARTIFACT_KINDS = ("merged", "firmware", "spiffs")
BOARD_FIELDS = ("id", "chip", "display", "touch")
BOARDS_JSON = Path(".github/boards.json")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def checksums(asset_dir: Path | None) -> dict[str, str]:
    if asset_dir is None:
        return {}
    return {path.name: sha256_of(path) for path in sorted(asset_dir.glob("*.bin"))}


def artifact(chip: str, tag: str, kind: str, sums: dict[str, str]) -> dict:
    name = f"canshift-{chip}-{tag}-{kind}.bin"
    return {"file": name, "sha256": sums.get(name)}


def build_manifest(boards: list[dict], tag: str, version: str, sums: dict[str, str]) -> dict:
    chips = [b["chip"] for b in boards if b.get("universal")]
    return {
        "schema": SCHEMA,
        "version": version,
        "tag": tag,
        "firmwares": {
            chip: {kind: artifact(chip, tag, kind, sums) for kind in ARTIFACT_KINDS}
            for chip in chips
        },
        "boards": [
            {field: board[field] for field in BOARD_FIELDS}
            for board in boards
            if board.get("release")
        ],
    }


def orphan_boards(manifest: dict) -> list[str]:
    return [b["id"] for b in manifest["boards"] if b["chip"] not in manifest["firmwares"]]


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    asset_dir = Path(argv[3]) if len(argv) > 3 else None
    boards = json.loads(BOARDS_JSON.read_text())
    manifest = build_manifest(boards, argv[1], argv[2], checksums(asset_dir))

    orphans = orphan_boards(manifest)
    if orphans:
        print(
            f"::error title=manifest::no universal firmware for {sorted(orphans)}",
            file=sys.stderr,
        )
        return 1

    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
