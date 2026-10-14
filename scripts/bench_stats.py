import argparse
import json
import sys
from pathlib import Path

SCHEMA = 1
MAX_ENTRIES = 500
TREND_POINTS = 30
SPARKS = "▁▂▃▄▅▆▇█"
SIZE_METRICS = ("flash", "dram", "iram", "psram")
CALIBRATION = "calibration"
COMMENT_MARKER = "<!-- canshift-bench -->"


class StatsError(Exception):
    pass


def read_json(path: Path | None) -> dict | None:
    if path is None or not path.exists():
        return None
    return json.loads(path.read_text())


def read_sizes(directory: Path | None) -> dict:
    if directory is None or not directory.exists():
        return {}
    reports = [json.loads(path.read_text()) for path in sorted(directory.rglob("size-*.json"))]
    return {report["board"]: report["sizes"] for report in reports}


def perf_medians(report: dict | None) -> dict:
    if report is None:
        return {}
    return {case["name"]: case["head_ns"] for case in report["cases"]}


def pass_counts(report: dict | None) -> dict | None:
    if report is None:
        return None
    return {"passed": report["passed"], "failed": report["failed"]}


def load_history(path: Path) -> dict:
    history = read_json(path)
    if history is None:
        return {"schema": SCHEMA, "entries": []}
    if history.get("schema") != SCHEMA:
        raise StatsError(f"{path}: schema {history.get('schema')} is not {SCHEMA}")
    return history


def build_entry(args: argparse.Namespace) -> dict:
    return {
        "sha": args.sha,
        "date": args.date,
        "sizes": read_sizes(args.sizes),
        "perf": perf_medians(read_json(args.perf)),
        "goldens": pass_counts(read_json(args.goldens)),
        "visual": pass_counts(read_json(args.visual)),
    }


def run_record(args: argparse.Namespace) -> int:
    history = load_history(args.history)
    entries = [entry for entry in history["entries"] if entry["sha"] != args.sha]
    entries.append(build_entry(args))
    history["entries"] = entries[-MAX_ENTRIES:]
    args.history.write_text(json.dumps(history, indent=1) + "\n")
    print(f"[stats] {args.history}: {len(history['entries'])} entries, latest {args.sha[:7]}")
    return 0


def sparkline(values: list[float]) -> str:
    if not values:
        return ""
    low, high = min(values), max(values)
    if high == low:
        return SPARKS[len(SPARKS) // 2] * len(values)
    scale = (len(SPARKS) - 1) / (high - low)
    return "".join(SPARKS[round((value - low) * scale)] for value in values)


def series(entries: list[dict], pick) -> list[float]:
    values = [pick(entry) for entry in entries[-TREND_POINTS:]]
    return [value for value in values if value is not None]


def size_of(entry: dict, board: str, metric: str) -> float | None:
    return entry["sizes"].get(board, {}).get(metric)


def size_rows(entries: list[dict]) -> list[str]:
    latest = entries[-1]["sizes"]
    rows = ["| board | metric | bytes | trend |", "| --- | --- | ---: | --- |"]
    for board in sorted(latest):
        for metric in SIZE_METRICS:
            trend = sparkline(series(entries, lambda e: size_of(e, board, metric)))
            rows.append(f"| {board} | {metric} | {latest[board][metric]:,} | `{trend}` |")
    return rows


def normalised(entry: dict, case: str) -> float | None:
    calibration = entry["perf"].get(CALIBRATION)
    value = entry["perf"].get(case)
    return value / calibration if value is not None and calibration else None


def perf_rows(entries: list[dict]) -> list[str]:
    latest = entries[-1]["perf"]
    rows = ["| case | median ns | trend (÷ calibration) |", "| --- | ---: | --- |"]
    for case in latest:
        trend = sparkline(series(entries, lambda e: normalised(e, case)))
        rows.append(f"| {case} | {latest[case]:,.1f} | `{trend}` |")
    return rows


def golden_cell(counts: dict | None) -> str:
    if counts is None:
        return "—"
    return f"{counts['passed']}/{counts['passed'] + counts['failed']}"


def golden_rows(entries: list[dict]) -> list[str]:
    rows = ["| commit | date | behaviour | visual |", "| --- | --- | ---: | ---: |"]
    for entry in reversed(entries[-10:]):
        rows.append(
            f"| `{entry['sha'][:7]}` | {entry['date']} | {golden_cell(entry['goldens'])} | {golden_cell(entry['visual'])} |"
        )
    return rows


def readme(history: dict) -> str:
    entries = history["entries"]
    if not entries:
        raise StatsError("history holds no entries")
    latest = entries[-1]
    parts = [
        "# CANShift firmware bench",
        "",
        f"Latest: `{latest['sha'][:7]}` on {latest['date']}, {len(entries)} commits recorded. "
        f"Trends cover the last {TREND_POINTS} commits on `main`; the raw data is `history.json`.",
        "",
        "## Image sizes",
        "",
        *size_rows(entries),
        "",
        "## Hot paths",
        "",
        *perf_rows(entries),
        "",
        "## Goldens",
        "",
        *golden_rows(entries),
    ]
    return "\n".join(parts) + "\n"


def run_readme(args: argparse.Namespace) -> int:
    args.out.write_text(readme(load_history(args.history)))
    return 0


def size_delta_rows(current: dict, baseline: dict) -> list[str]:
    rows = ["| board | metric | main | PR | delta |", "| --- | --- | ---: | ---: | ---: |"]
    for board in sorted(current):
        for metric in SIZE_METRICS:
            base = baseline.get(board, {}).get(metric)
            head = current[board][metric]
            delta = f"{head - base:+,}" if base is not None else "—"
            base_cell = f"{base:,}" if base is not None else "—"
            rows.append(f"| {board} | {metric} | {base_cell} | {head:,} | {delta} |")
    return rows


def perf_delta_rows(report: dict) -> list[str]:
    rows = ["| case | base ns | PR ns | delta | noise | verdict |", "| --- | ---: | ---: | ---: | ---: | --- |"]
    for case in report["cases"]:
        base = f"{case['base_ns']:,.1f}" if "base_ns" in case else "—"
        delta = f"{(case['ratio'] - 1) * 100:+.1f} %" if "ratio" in case else "—"
        noise = max(case["head_spread"], case.get("base_spread", 0.0)) * 100
        rows.append(f"| {case['name']} | {base} | {case['head_ns']:,.1f} | {delta} | {noise:.1f} % | {case['verdict']} |")
    return rows


def comment_sections(args: argparse.Namespace, baseline: dict) -> list[str]:
    sections = []
    sizes = read_sizes(args.sizes)
    if sizes:
        sections += ["### Image sizes (against the latest main in `bench-data`)", "", *size_delta_rows(sizes, baseline.get("sizes", {})), ""]
    perf = read_json(args.perf)
    if perf is not None:
        sections += [f"### Hot paths (against {perf['baseline']})", "", *perf_delta_rows(perf), ""]
    goldens, visual = pass_counts(read_json(args.goldens)), pass_counts(read_json(args.visual))
    sections += [f"### Goldens\n\nbehaviour {golden_cell(goldens)} · visual {golden_cell(visual)}", ""]
    return sections


def run_comment(args: argparse.Namespace) -> int:
    entries = load_history(args.history)["entries"] if args.history.exists() else []
    baseline = entries[-1] if entries else {}
    header = [COMMENT_MARKER, "## Bench", ""]
    if baseline:
        header += [f"Main baseline: `{baseline['sha'][:7]}` ({baseline['date']}).", ""]
    args.out.write_text("\n".join(header + comment_sections(args, baseline)))
    return 0


def add_inputs(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--history", type=Path, required=True)
    parser.add_argument("--sizes", type=Path)
    parser.add_argument("--perf", type=Path)
    parser.add_argument("--goldens", type=Path)
    parser.add_argument("--visual", type=Path)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="bench_stats.py")
    commands = parser.add_subparsers(dest="command", required=True)
    record = commands.add_parser("record")
    add_inputs(record)
    record.add_argument("--sha", required=True)
    record.add_argument("--date", required=True)
    record.set_defaults(handler=run_record)
    readme_cmd = commands.add_parser("readme")
    readme_cmd.add_argument("--history", type=Path, required=True)
    readme_cmd.add_argument("--out", type=Path, required=True)
    readme_cmd.set_defaults(handler=run_readme)
    comment = commands.add_parser("comment")
    add_inputs(comment)
    comment.add_argument("--out", type=Path, required=True)
    comment.set_defaults(handler=run_comment)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        return args.handler(args)
    except (OSError, StatsError, KeyError, ValueError) as error:
        sys.stderr.write(f"[stats] {error}\n")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
