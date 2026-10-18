import argparse
import json
import os
import re
import sys

import lizard
from lizard_languages import get_reader_for

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE_PATH = os.path.join(ROOT, "scripts", "code_shape_baseline.json")
SOURCE_DIRS = ("src", "include", "rust", "test", "sim", "bench")
SOURCE_EXTENSIONS = (".h", ".cpp", ".inc", ".c", ".rs")
EXCLUDED = (
    "src/ui/fonts/",
    "src/ui/monogram_baked.cpp",
    "src/ui/icon_assets_baked.cpp",
    "src/ui/monogram_baked.h",
    "include/lv_conf.h",
    "test/native/vendor/",
    "rust/target/",
)
MAX_FUNCTION_LINES = 30
MAX_NESTING = 2
MAX_FILE_LINES = 300
RUST_CHAR_LITERAL = re.compile(
    r"b?'(?:\\(?:x[0-9a-fA-F]{2}|u\{[0-9a-fA-F]+\}|.)|[^'\\\n])'"
)
LOAD_BEARING_COMMENT = re.compile(r"^(//|/\*)\s*(NOLINT|clang-format (on|off))")
RULES = {
    "function-lines": f"function longer than {MAX_FUNCTION_LINES} code lines",
    "nesting": f"control flow nested deeper than {MAX_NESTING}",
    "file-lines": f"file longer than {MAX_FILE_LINES} code lines",
    "comments": "comments (zero-comment rule)",
}
ANALYZER = lizard.FileAnalyzer(lizard.get_extensions(["ns"]))


def source_files():
    for top in SOURCE_DIRS:
        for directory, _, names in os.walk(os.path.join(ROOT, top)):
            for name in sorted(names):
                rel = os.path.relpath(os.path.join(directory, name), ROOT).replace(
                    os.sep, "/"
                )
                if name.endswith(SOURCE_EXTENSIONS) and not rel.startswith(EXCLUDED):
                    yield rel


def reader_path(rel):
    return rel[: -len(".inc")] + ".h" if rel.endswith(".inc") else rel


def measurable(rel, code):
    if not rel.endswith(".rs"):
        return code
    return RUST_CHAR_LITERAL.sub(lambda literal: "0" * len(literal.group()), code)


def comment_count(rel, code):
    tokens = get_reader_for(reader_path(rel)).generate_tokens(code)
    comments = (token for token in tokens if token.startswith(("//", "/*")))
    return sum(1 for token in comments if not LOAD_BEARING_COMMENT.match(token))


def record(findings, rule, key, value, limit):
    if value > limit:
        findings[rule][key] = max(value, findings[rule].get(key, 0))


def analyse(rel, findings):
    with open(
        os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace"
    ) as handle:
        code = handle.read()
    info = ANALYZER.analyze_source_code(reader_path(rel), measurable(rel, code))
    for function in info.function_list:
        key = f"{rel}::{function.name}"
        record(findings, "function-lines", key, function.nloc, MAX_FUNCTION_LINES)
        record(findings, "nesting", key, function.max_nested_structures, MAX_NESTING)
    record(findings, "file-lines", rel, info.nloc, MAX_FILE_LINES)
    record(findings, "comments", rel, comment_count(rel, code), 0)


def current_findings():
    findings = {rule: {} for rule in RULES}
    for rel in source_files():
        analyse(rel, findings)
    return findings


def load_baseline():
    if not os.path.exists(BASELINE_PATH):
        return {rule: {} for rule in RULES}
    with open(BASELINE_PATH, "r", encoding="utf-8") as handle:
        stored = json.load(handle)
    return {rule: stored.get(rule, {}) for rule in RULES}


def regressions(findings, baseline):
    for rule, entries in findings.items():
        for key, value in sorted(entries.items()):
            allowed = baseline[rule].get(key)
            if allowed is None or value > allowed:
                yield rule, key, value, allowed


def stale_entries(findings, baseline):
    for rule, entries in baseline.items():
        for key, allowed in sorted(entries.items()):
            if findings[rule].get(key, 0) < allowed:
                yield rule, key, findings[rule].get(key, 0), allowed


def write_baseline(findings):
    ordered = {rule: dict(sorted(findings[rule].items())) for rule in RULES}
    with open(BASELINE_PATH, "w", encoding="utf-8") as handle:
        json.dump(ordered, handle, indent=2)
        handle.write("\n")


def report(title, rows):
    print(title)
    for rule, key, value, allowed in rows:
        print(f"  {key}: {value} (baseline {allowed}) — {RULES[rule]}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    findings = current_findings()
    baseline = load_baseline()
    new = list(regressions(findings, baseline))
    stale = list(stale_entries(findings, baseline))
    if new:
        report(
            "New code-shape violations — split, flatten or remove before merging:", new
        )
        return 1
    if args.update:
        write_baseline(findings)
        return 0
    if stale:
        report(
            "The baseline is above the code — run scripts/check_code_shape.py --update:",
            stale,
        )
        return 1
    total = sum(len(entries) for entries in baseline.values())
    print(f"code shape: no new violations ({total} baselined)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
