#!/usr/bin/env python3

import json
import re
import sys
from pathlib import Path

UNIT_FACE = Path("src/ui/fonts/lv_font_jbmono_medium_10_nk.c")
PRESENTATION = Path("src/ui/signal_presentation.cpp")
UNIT_PAIRS = Path("rust/unit-convert/src/lib.rs")
BUNDLED_SIGNALS = Path("data/config/signals.json")
BUNDLED_DASHBOARD = Path("data/config/dashboard.json")

CMAP_ENTRY = re.compile(
    r"\.range_start = (\d+), \.range_length = (\d+),.*?"
    r"\.unicode_list = (\w+),.*?\.type = (LV_FONT_FMT_TXT_CMAP_\w+)",
    re.DOTALL,
)
PRESENTATION_ENTRY = re.compile(r'\{"(\w+)", "[^"]*", "((?:[^"\\]|\\.)*)"\}')
UNIT_PAIR_SYMBOL = re.compile(r'(metric|imperial): b?"([^"]*)\\0"')


def unicode_list(text: str, name: str) -> list[int]:
    match = re.search(rf"{name}\[\] = \{{([^}}]*)\}}", text)
    if match is None:
        sys.exit(f"{UNIT_FACE}: no {name} array")
    return [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", match.group(1))]


def cmap_codepoints(
    text: str, start: int, length: int, list_name: str, kind: str
) -> set[int]:
    if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
        return set(range(start, start + length))
    if kind == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
        return {start + offset for offset in unicode_list(text, list_name)}
    sys.exit(f"{UNIT_FACE}: cmap type {kind} is not read by this check")


def face_codepoints() -> set[int]:
    text = UNIT_FACE.read_text()
    codepoints: set[int] = set()
    for start, length, list_name, kind in CMAP_ENTRY.findall(text):
        codepoints |= cmap_codepoints(text, int(start), int(length), list_name, kind)
    if not codepoints:
        sys.exit(f"{UNIT_FACE}: no cmap entries found")
    return codepoints


def presentation_units() -> list[tuple[str, str]]:
    entries = PRESENTATION_ENTRY.findall(PRESENTATION.read_text())
    if not entries:
        sys.exit(f"{PRESENTATION}: no presentation entries found")
    return [
        (f"{PRESENTATION} {signal_id}", raw.encode().decode("unicode_escape"))
        for signal_id, raw in entries
    ]


def unit_pair_symbols() -> list[tuple[str, str]]:
    symbols = UNIT_PAIR_SYMBOL.findall(UNIT_PAIRS.read_text())
    if not symbols:
        sys.exit(f"{UNIT_PAIRS}: no unit pair symbols found")
    return [(f"{UNIT_PAIRS} {system}", symbol) for system, symbol in symbols]


def bundled_signal_units() -> list[tuple[str, str]]:
    signals = json.loads(BUNDLED_SIGNALS.read_text())["signals"]
    return [
        (f"{BUNDLED_SIGNALS} {signal['name']}", signal.get("unit", ""))
        for signal in signals
    ]


def suffixes_in(node: object) -> list[str]:
    if isinstance(node, list):
        return [suffix for item in node for suffix in suffixes_in(item)]
    if not isinstance(node, dict):
        return []
    own = [node["suffix"]] if isinstance(node.get("suffix"), str) else []
    return own + [suffix for value in node.values() for suffix in suffixes_in(value)]


def bundled_suffixes() -> list[tuple[str, str]]:
    dashboard = json.loads(BUNDLED_DASHBOARD.read_text())
    return [
        (f"{BUNDLED_DASHBOARD} suffix", suffix) for suffix in suffixes_in(dashboard)
    ]


def missing_glyphs(units: list[tuple[str, str]], glyphs: set[int]) -> list[str]:
    return [
        f"{origin}: '{unit}' needs U+{ord(char):04X}, which {UNIT_FACE.name} does not carry"
        for origin, unit in units
        for char in unit
        if ord(char) not in glyphs
    ]


def main() -> int:
    units = presentation_units() + unit_pair_symbols() + bundled_signal_units()
    units += bundled_suffixes()
    failures = missing_glyphs(units, face_codepoints())
    for failure in failures:
        print(failure)
    if failures:
        print(
            "Add the codepoint to UNIT_RANGE in scripts/regen_fonts.py and regenerate."
        )
        return 1
    print(f"{len(units)} unit strings render with glyphs present in {UNIT_FACE.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
