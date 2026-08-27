#!/usr/bin/env python3
"""Gate the display tier catalog against the one the tuner renders from.

    include/display_tiers.h        what the firmware lays out and picks font faces from
    core src/display-tiers.ts      what the tuner's canvas preview resolves   (when a
                                   sibling checkout exists)

A tier is a design space, a grid and a font ladder. Both sides must agree on all
three or the preview stops being a preview. Run from the repository root.
"""

import re
import sys
from pathlib import Path

HEADER = Path("include/display_tiers.h")
CORE_TIERS = Path("../canshift-core/src/display-tiers.ts")
CORE_CAPS = Path("../canshift-core/src/constants/firmware-caps.ts")
CORE_GRID = Path("../canshift-core/src/layout-grid.ts")

FIELDS = ("designWidth", "designHeight", "columns", "rows", "maxWidgetsPerPage")

HEADER_ENTRY = re.compile(
    r'\{"(\w+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*'
    r"tiers::ladderOf\(tiers::(\w+)\),\s*tiers::ladderOf\(tiers::(\w+)\)\}",
    re.DOTALL,
)


def numbers_in(text: str, name: str) -> list[int]:
    match = re.search(rf"{name}\[\]\s*=\s*\{{([^}}]*)\}}", text, re.DOTALL)
    if match is None:
        return []
    return [int(n) for n in re.findall(r"\d+", match.group(1))]


def read_header() -> dict[str, dict]:
    text = HEADER.read_text()
    tiers = {}
    for entry in HEADER_ENTRY.finditer(text):
        tier_id, *dims, value_sym, label_sym = entry.groups()
        tiers[tier_id] = {
            **{field: int(value) for field, value in zip(FIELDS, dims)},
            "valueFaces": numbers_in(text, value_sym),
            "labelFaces": numbers_in(text, label_sym),
        }
    return tiers


def core_symbols() -> dict[str, int]:
    caps = CORE_CAPS.read_text()
    grid = CORE_GRID.read_text()
    symbols = {}
    for source, names in ((caps, ("WIDTH", "HEIGHT", "MAX_WIDGETS_PER_PAGE")),
                          (grid, ("COLUMNS", "ROWS"))):
        for name in names:
            match = re.search(rf"\b{name}:\s*(\d+)", source)
            if match is not None:
                symbols[name] = int(match.group(1))
    return symbols


def resolve(raw: str, symbols: dict[str, int]) -> int | None:
    raw = raw.strip().rstrip(",")
    if raw.isdigit():
        return int(raw)
    return symbols.get(raw.split(".")[-1])


def read_core(symbols: dict[str, int]) -> dict[str, dict]:
    text = CORE_TIERS.read_text()
    tiers = {}
    for block in re.finditer(r"\{\s*id:\s*'(\w+)',(.*?)\n  \}", text, re.DOTALL):
        tier_id, body = block.group(1), block.group(2)
        tier = {}
        for field in FIELDS:
            match = re.search(rf"{field}:\s*([^,\n]+)", body)
            tier[field] = resolve(match.group(1), symbols) if match else None
        for ladder in ("valueFaces", "labelFaces"):
            match = re.search(rf"{ladder}:\s*\[([^\]]*)\]", body)
            tier[ladder] = [int(n) for n in re.findall(r"\d+", match.group(1))] if match else []
        tiers[tier_id] = tier
    return tiers


def compare(header: dict[str, dict], core: dict[str, dict]) -> list[str]:
    problems = []
    for missing in sorted(set(core) - set(header)):
        problems.append(f"core declares tier '{missing}' and the firmware does not")
    for extra in sorted(set(header) - set(core)):
        problems.append(f"the firmware declares tier '{extra}' and core does not")
    for tier_id in sorted(set(header) & set(core)):
        for field in (*FIELDS, "valueFaces", "labelFaces"):
            mine, theirs = header[tier_id][field], core[tier_id][field]
            if mine != theirs:
                problems.append(f"{tier_id}.{field}: firmware {mine}, core {theirs}")
    return problems


def main() -> int:
    header = read_header()
    if not header:
        print(f"[tiers] no tier entries parsed from {HEADER}", file=sys.stderr)
        return 1

    if not CORE_TIERS.exists():
        print("[tiers] no sibling canshift-core checkout — skipping tier parity")
        return 0

    problems = compare(header, read_core(core_symbols()))
    if problems:
        for problem in problems:
            print(f"[tiers] {problem}", file=sys.stderr)
        return 1

    print(f"[tiers] {len(header)} display tiers agree with core")
    return 0


if __name__ == "__main__":
    sys.exit(main())
