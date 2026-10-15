import argparse
import difflib
import json
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

PROGRAM = Path(".pio/build/bench/program")
GOLDENS = Path("bench/goldens")
DATA = Path("data")
TRANSPORTS = ("usb", "ws")
DIFF_LINES = 80


class GoldenError(Exception):
    pass


@dataclass
class Case:
    name: str
    golden: Path
    produce: Callable[[Path, Path], str]


def staged_data(scratch: Path, signals: Path | None) -> Path:
    root = Path(tempfile.mkdtemp(dir=scratch))
    shutil.copytree(DATA, root, dirs_exist_ok=True)
    if signals is not None:
        shutil.copyfile(signals, root / "config" / "signals.json")
    return root


def run_program(program: Path, arguments: list[str], out: Path) -> str:
    result = subprocess.run(
        [str(program), *arguments, "--out", str(out)], capture_output=True, text=True
    )
    if result.returncode != 0:
        raise GoldenError(
            f"{program} {arguments[0]} exited {result.returncode}: {result.stderr.strip()}"
        )
    return out.read_text()


def trace_case(case_dir: Path) -> Case:
    signals = case_dir / "signals.json"
    overlay = signals if signals.exists() else None

    def produce(program: Path, scratch: Path) -> str:
        data = staged_data(scratch, overlay)
        arguments = [
            "trace",
            "--trace",
            str(case_dir / "trace.txt"),
            "--data",
            str(data),
        ]
        return run_program(program, arguments, scratch / f"{case_dir.name}.timeline")

    return Case(f"can/{case_dir.name}", case_dir / "timeline.golden", produce)


def transcript_case(inputs: Path) -> Case:
    def produce(program: Path, scratch: Path) -> str:
        sections = []
        for transport in TRANSPORTS:
            data = staged_data(scratch, None)
            arguments = [
                "transcript",
                "--in",
                str(inputs),
                "--transport",
                transport,
                "--data",
                str(data),
            ]
            sections.append(
                f"== {transport}\n"
                + run_program(
                    program, arguments, scratch / f"{inputs.stem}.{transport}"
                )
            )
        return "".join(sections)

    return Case(f"protocol/{inputs.stem}", inputs.with_suffix(".golden"), produce)


def discover() -> list[Case]:
    traces = [
        trace_case(path.parent)
        for path in sorted((GOLDENS / "can").glob("*/trace.txt"))
    ]
    transcripts = [
        transcript_case(path) for path in sorted((GOLDENS / "protocol").glob("*.in"))
    ]
    return traces + transcripts


def diff_of(expected: str, actual: str, name: str) -> str:
    lines = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        f"{name} (golden)",
        f"{name} (actual)",
    )
    return "".join(list(lines)[:DIFF_LINES])


def check(case: Case, actual: str, update: bool, failures_dir: Path | None) -> str:
    if update:
        case.golden.write_text(actual)
        return "updated"
    expected = case.golden.read_text() if case.golden.exists() else ""
    if expected == actual:
        return "pass"
    sys.stderr.write(
        diff_of(expected, actual, case.name) or f"{case.golden} is missing\n"
    )
    if failures_dir is not None:
        target = failures_dir / f"{case.name.replace('/', '__')}.actual"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(actual)
    return "fail"


def run_cases(args: argparse.Namespace) -> list[dict]:
    results = []
    with tempfile.TemporaryDirectory() as scratch:
        for case in discover():
            actual = case.produce(args.program, Path(scratch))
            results.append(
                {
                    "name": case.name,
                    "status": check(case, actual, args.update_goldens, args.failures),
                }
            )
    return results


def format_table(results: list[dict]) -> str:
    passed = sum(1 for result in results if result["status"] != "fail")
    lines = [
        f"### Behaviour goldens: {passed}/{len(results)} pass",
        "",
        "| golden | result |",
        "| --- | --- |",
    ]
    return (
        "\n".join(lines + [f"| {r['name']} | {r['status']} |" for r in results]) + "\n"
    )


def write_outputs(args: argparse.Namespace, results: list[dict]) -> None:
    table = format_table(results)
    print(table)
    if args.summary is not None:
        with args.summary.open("a", encoding="utf-8") as handle:
            handle.write(table)
    if args.report is not None:
        passed = sum(1 for result in results if result["status"] != "fail")
        report = {"passed": passed, "failed": len(results) - passed, "cases": results}
        args.report.write_text(json.dumps(report, indent=2) + "\n")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="bench_goldens.py")
    parser.add_argument("--program", type=Path, default=PROGRAM)
    parser.add_argument("--update-goldens", action="store_true")
    parser.add_argument("--failures", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--summary", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        results = run_cases(args)
    except (OSError, GoldenError) as error:
        sys.stderr.write(f"[goldens] {error}\n")
        return 1
    write_outputs(args, results)
    return 1 if any(result["status"] == "fail" for result in results) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
