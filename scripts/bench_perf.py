import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

PROGRAM = Path(".pio/build/bench/program")
ACCEPT_FILE = Path("bench/perf-accept.json")
ACCEPT_FIELDS = ("case", "max_delta_pct", "pr", "reason")
REGRESSION_LIMIT = 1.10
IMPROVEMENT_LIMIT = 0.90
DEFAULT_ROUNDS = 5
CONFIRM_ROUNDS = 5
DEFAULT_SAMPLES = 31


class BenchError(Exception):
    pass


@dataclass
class Side:
    root: Path
    rounds: dict[str, list[float]] = field(default_factory=dict)
    units: dict[str, str] = field(default_factory=dict)


def run_once(side: Side, samples: int, case_filter: str | None) -> list[dict]:
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / "perf.json"
        cmd = [str(side.root / PROGRAM), "perf", "--out", str(out), "--samples", str(samples)]
        cmd += ["--filter", case_filter] if case_filter else []
        result = subprocess.run(cmd, cwd=side.root, capture_output=True, text=True)
        if result.returncode != 0:
            raise BenchError(f"{side.root / PROGRAM} exited {result.returncode}: {result.stderr.strip()}")
        return json.loads(out.read_text())["cases"]


def record(side: Side, cases: list[dict]) -> None:
    for case in cases:
        side.rounds.setdefault(case["name"], []).append(case["median_ns"])
        side.units[case["name"]] = case["unit"]


def run_rounds(sides: list[Side], rounds: int, samples: int, case_filter: str | None = None) -> None:
    for index in range(rounds):
        order = sides if index % 2 == 0 else list(reversed(sides))
        for side in order:
            record(side, run_once(side, samples, case_filter))


def center(values: list[float]) -> float:
    return statistics.median(values)


def spread(values: list[float]) -> float:
    return (max(values) - min(values)) / center(values)


def verdict(base: list[float], head: list[float]) -> str:
    ratio = center(head) / center(base)
    if ratio < IMPROVEMENT_LIMIT:
        return "faster"
    if ratio <= REGRESSION_LIMIT:
        return "ok"
    return "regression" if min(head) > max(base) else "noisy"


def case_row(name: str, base: Side, head: Side) -> dict:
    head_rounds = head.rounds[name]
    row = {"name": name, "unit": head.units[name], "head_ns": center(head_rounds), "head_spread": spread(head_rounds)}
    if name not in base.rounds:
        return {**row, "verdict": "new"}
    base_rounds = base.rounds[name]
    ratio = row["head_ns"] / center(base_rounds)
    return {
        **row,
        "base_ns": center(base_rounds),
        "base_spread": spread(base_rounds),
        "ratio": ratio,
        "verdict": verdict(base_rounds, head_rounds),
    }


def compare_rows(base: Side, head: Side) -> list[dict]:
    return [case_row(name, base, head) for name in head.rounds]


def flagged(rows: list[dict]) -> list[str]:
    return [row["name"] for row in rows if row["verdict"] == "regression"]


def confirm(base: Side, head: Side, names: list[str], samples: int) -> None:
    for name in names:
        run_rounds([base, head], CONFIRM_ROUNDS, samples, name)


def load_acceptances(path: Path, pr: int | None) -> dict[str, dict]:
    if pr is None or not path.exists():
        return {}
    entries = json.loads(path.read_text())["accepted"]
    for entry in entries:
        missing = [name for name in ACCEPT_FIELDS if name not in entry]
        if missing:
            raise BenchError(f"{path}: entry {entry} lacks {', '.join(missing)}")
    return {entry["case"]: entry for entry in entries if entry["pr"] == pr}


def apply_acceptance(row: dict, acceptances: dict[str, dict]) -> dict:
    entry = acceptances.get(row["name"])
    if row["verdict"] != "regression" or entry is None:
        return row
    if (row["ratio"] - 1) * 100 > entry["max_delta_pct"]:
        return row
    return {**row, "verdict": f"accepted ({entry['reason']})"}


def format_row(row: dict) -> str:
    base = f"{row['base_ns']:,.1f}" if "base_ns" in row else "—"
    ratio = f"{(row['ratio'] - 1) * 100:+.1f} %" if "ratio" in row else "—"
    noise = max(row["head_spread"], row.get("base_spread", 0.0)) * 100
    return f"| {row['name']} | {row['unit']} | {base} | {row['head_ns']:,.1f} | {ratio} | {noise:.1f} % | {row['verdict']} |"


def format_table(rows: list[dict], baseline: str) -> str:
    lines = [
        f"### Performance bench (baseline: {baseline})",
        "",
        "| case | unit | base | head | delta | noise | verdict |",
        "| --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    return "\n".join(lines + [format_row(row) for row in rows]) + "\n"


def write_outputs(args: argparse.Namespace, rows: list[dict], baseline: str) -> None:
    table = format_table(rows, baseline)
    print(table)
    if args.summary is not None:
        with args.summary.open("a", encoding="utf-8") as handle:
            handle.write(table)
    if args.report is not None:
        args.report.write_text(json.dumps({"baseline": baseline, "cases": rows}, indent=2) + "\n")


def run_compare(args: argparse.Namespace) -> int:
    head = Side(args.head_root.resolve())
    base = Side((args.base_root or args.head_root).resolve())
    baseline = args.baseline_label if args.base_root is not None else "head (A/A, no baseline bench)"
    run_rounds([base, head], args.rounds, args.samples)
    rows = compare_rows(base, head)
    suspects = flagged(rows)
    if suspects:
        confirm(base, head, suspects, args.samples)
        rows = compare_rows(base, head)
    acceptances = load_acceptances(args.accept, args.pr)
    rows = [apply_acceptance(row, acceptances) for row in rows]
    write_outputs(args, rows, baseline)
    regressions = flagged(rows)
    for name in regressions:
        sys.stderr.write(f"[perf] {name}: median regressed past {REGRESSION_LIMIT:.0%} of the baseline\n")
    return 1 if regressions else 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="bench_perf.py")
    parser.add_argument("--head-root", type=Path, default=Path("."))
    parser.add_argument("--base-root", type=Path)
    parser.add_argument("--baseline-label", default="main")
    parser.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--accept", type=Path, default=ACCEPT_FILE)
    parser.add_argument("--pr", type=int)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        return run_compare(args)
    except (OSError, BenchError, KeyError, ValueError) as error:
        sys.stderr.write(f"[perf] {error}\n")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
