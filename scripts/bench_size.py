import argparse
import json
import struct
import sys
from pathlib import Path

ELF_MAGIC = b"\x7fELF"
ELF_CLASS_32 = 1
SECTION_HEADER_FORMAT = "<IIIIIIIIII"
METRICS = ("flash", "dram", "iram", "psram")
REGION_PREFIXES = {
    "dram": (".dram0.data", ".dram0.bss", ".noinit"),
    "iram": (".iram0.",),
    "psram": (".ext_ram",),
}


class SizeError(Exception):
    pass


def read_header_fields(blob: bytes) -> tuple[int, int, int, int]:
    if blob[:4] != ELF_MAGIC or blob[4] != ELF_CLASS_32:
        raise SizeError("not a 32-bit ELF image")
    shoff = struct.unpack_from("<I", blob, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 0x2E)
    return shoff, shentsize, shnum, shstrndx


def section_headers(blob: bytes) -> list[tuple[int, ...]]:
    shoff, shentsize, shnum, _ = read_header_fields(blob)
    return [struct.unpack_from(SECTION_HEADER_FORMAT, blob, shoff + i * shentsize) for i in range(shnum)]


def section_name(blob: bytes, names_offset: int, name_index: int) -> str:
    start = names_offset + name_index
    return blob[start : blob.index(b"\0", start)].decode("ascii")


def section_sizes(elf: Path) -> dict[str, int]:
    blob = elf.read_bytes()
    headers = section_headers(blob)
    names_offset = headers[read_header_fields(blob)[3]][4]
    return {section_name(blob, names_offset, header[0]): header[5] for header in headers}


def region_size(sections: dict[str, int], prefixes: tuple[str, ...]) -> int:
    return sum(size for name, size in sections.items() if name.startswith(prefixes))


def measure(board: str, elf: Path, image: Path) -> dict:
    sections = section_sizes(elf)
    sizes = {region: region_size(sections, prefixes) for region, prefixes in REGION_PREFIXES.items()}
    sizes["flash"] = image.stat().st_size
    return {"board": board, "sizes": {metric: sizes[metric] for metric in METRICS}}


def load_measurements(directory: Path) -> dict[str, dict[str, int]]:
    reports = [json.loads(path.read_text()) for path in sorted(directory.rglob("size-*.json"))]
    return {report["board"]: report["sizes"] for report in reports}


def format_delta(delta: int) -> str:
    return f"{delta:+,}" if delta else "0"


def metric_row(board: str, metric: str, budget: int, actual: int, tolerance: int) -> tuple[str, bool]:
    over = actual > budget + tolerance
    status = "over budget" if over else "ok"
    row = f"| {board} | {metric} | {budget:,} | {actual:,} | {format_delta(actual - budget)} | {status} |"
    return row, over


def board_rows(board: str, budget: dict[str, int], actual: dict[str, int], tolerance: dict[str, int]):
    return [metric_row(board, metric, budget[metric], actual[metric], tolerance[metric]) for metric in METRICS]


def missing_boards(budget: dict, measured: dict) -> list[str]:
    return sorted(set(budget["boards"]) ^ set(measured))


def compare(budget: dict, measured: dict[str, dict[str, int]]) -> tuple[list[str], bool]:
    lines = ["| board | metric | budget | actual | delta | status |", "| --- | --- | ---: | ---: | ---: | --- |"]
    failed = False
    for board in sorted(budget["boards"]):
        for row, over in board_rows(board, budget["boards"][board], measured[board], budget["tolerance"]):
            lines.append(row)
            failed = failed or over
    return lines, failed


def report_missing(missing: list[str]) -> int:
    sys.stderr.write(f"[size] budget and build matrix disagree on boards: {', '.join(missing)}\n")
    return 1


def check(budget_path: Path, measured_dir: Path, summary: Path | None) -> int:
    budget = json.loads(budget_path.read_text())
    measured = load_measurements(measured_dir)
    missing = missing_boards(budget, measured)
    if missing:
        return report_missing(missing)
    lines, failed = compare(budget, measured)
    table = "\n".join(lines) + "\n"
    print(table)
    if summary is not None:
        with summary.open("a", encoding="utf-8") as handle:
            handle.write("### Size budget\n\n" + table)
    if failed:
        sys.stderr.write(f"[size] a board grew past its budget plus tolerance; raise it in {budget_path} on purpose\n")
    return 1 if failed else 0


def run_measure(args: argparse.Namespace) -> int:
    report = measure(args.board, args.elf, args.bin)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    return 0


def run_check(args: argparse.Namespace) -> int:
    return check(args.budget, args.measured, args.summary)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="bench_size.py")
    commands = parser.add_subparsers(dest="command", required=True)
    measure_cmd = commands.add_parser("measure")
    measure_cmd.add_argument("--board", required=True)
    measure_cmd.add_argument("--elf", type=Path, required=True)
    measure_cmd.add_argument("--bin", type=Path, required=True)
    measure_cmd.add_argument("--out", type=Path, required=True)
    measure_cmd.set_defaults(handler=run_measure)
    check_cmd = commands.add_parser("check")
    check_cmd.add_argument("--budget", type=Path, default=Path("bench/size-budget.json"))
    check_cmd.add_argument("--measured", type=Path, required=True)
    check_cmd.add_argument("--summary", type=Path)
    check_cmd.set_defaults(handler=run_check)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        return args.handler(args)
    except (OSError, SizeError, KeyError, ValueError) as error:
        sys.stderr.write(f"[size] {error}\n")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
