#!/usr/bin/env python3
"""Gate the three lists that describe which boards CANShift supports.

    include/boards/*.h   what the firmware can be built for, and what catalog.h can select
    .github/boards.json  what CI builds, and which of those a release publishes
    core BOARD_PROFILES  what the tuner can offer   (checked when a sibling checkout exists)

The invariants: every header has a boards.json entry under the same id and a
PlatformIO env under that name, and core's catalog holds exactly the releasable
subset. Run from the repository root.
"""

import json
import re
import sys
from pathlib import Path

BOARDS_JSON = Path(".github/boards.json")
HEADERS_DIR = Path("include/boards")
PLATFORMIO = Path("platformio.ini")
CATALOG = HEADERS_DIR / "catalog.h"
CORE_CATALOG = Path("../canshift-core/src/board-profile/catalog.ts")


def board_id_of(header: Path) -> str | None:
    match = re.search(r"\.board_id\s*=\s*\"([^\"]+)\"", header.read_text())
    return match.group(1) if match else None


def report(problems: list[str]) -> int:
    if not problems:
        return 0
    for problem in problems:
        print(f"[boards] {problem}", file=sys.stderr)
    return 1


def board_headers() -> list[Path]:
    return [h for h in sorted(HEADERS_DIR.glob("*.h")) if h.name != "catalog.h"]


def check_headers(ids: set[str], problems: list[str]) -> set[str]:
    header_ids = set()
    for header in board_headers():
        board_id = board_id_of(header)
        if board_id is None:
            problems.append(f"{header}: no .board_id field")
            continue
        header_ids.add(board_id)
        if board_id != header.stem:
            problems.append(f"{header}: declares board_id '{board_id}'")
    for missing in sorted(header_ids - ids):
        problems.append(f"'{missing}' has a board header but no boards.json entry")
    for missing in sorted(ids - header_ids):
        problems.append(f"'{missing}' is in boards.json but has no board header")
    return header_ids


def check_catalog(problems: list[str]) -> None:
    listed = set(re.findall(r"&(k[A-Za-z0-9]+)", CATALOG.read_text()))
    for header in board_headers():
        match = re.search(r"constexpr BoardProfile (k\w+)", header.read_text())
        if match is None:
            problems.append(f"{header}: no profile symbol")
            continue
        if match.group(1) not in listed:
            problems.append(f"{header}: '{match.group(1)}' is missing from catalog.h")


def check_envs(ids: set[str], problems: list[str]) -> None:
    envs = set(re.findall(r"^\[env:([^\]]+)\]", PLATFORMIO.read_text(), re.MULTILINE))
    for missing in sorted(ids - envs):
        problems.append(f"'{missing}' is in boards.json but has no [env:{missing}]")


def check_core_catalog(releasable: set[str], problems: list[str]) -> None:
    if not CORE_CATALOG.exists():
        print("[boards] no sibling canshift-core checkout — skipping catalog parity")
        return
    catalog = set(re.findall(r"boardId:\s*'([^']+)'", CORE_CATALOG.read_text()))
    for missing in sorted(releasable - catalog):
        problems.append(f"'{missing}' is released but missing from core BOARD_PROFILES")
    for extra in sorted(catalog - releasable):
        problems.append(f"'{extra}' is in core BOARD_PROFILES but not released by this repo")


def main() -> int:
    boards = json.loads(BOARDS_JSON.read_text())
    ids = {b["id"] for b in boards}
    releasable = {b["id"] for b in boards if b.get("release")}

    problems: list[str] = []
    check_headers(ids, problems)
    check_catalog(problems)
    check_envs(ids, problems)
    check_core_catalog(releasable, problems)

    if problems:
        return report(problems)
    print(f"[boards] {len(ids)} boards agree across every list ({len(releasable)} releasable)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
