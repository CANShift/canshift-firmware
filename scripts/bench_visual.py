import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

PROGRAM = Path(".pio/build/sim/program")
DATA = Path("data")
GOLDENS = Path("bench/goldens/visual")
CAPTURE = "sim-screenshot.bmp"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
RGB = 3
DIFF_DIM = 4
DIFF_MARK = (255, 0, 0)
G_METER_DASHBOARD = Path("sim/fixtures/g-meter-dashboard.json")
TIERS = {
    "base": None,
    "medium": Path("sim/boards/sim_medium_480x320.json"),
    "large": Path("sim/boards/sim_large_800x480.json"),
}


class VisualError(Exception):
    pass


@dataclass(frozen=True)
class Scene:
    name: str
    scenario: str
    page: str
    capture_ms: int
    theme: str | None = None
    dashboard: Path | None = None


SCENES = (
    Scene("page-street", "cruise", "street", 1500),
    Scene("page-track", "cruise", "track", 1500),
    Scene("page-engine", "cruise", "engine", 1500),
    Scene("page-tuning", "cruise", "tuning", 1500),
    Scene("page-timing", "cruise", "timing", 1500),
    Scene("page-controls", "controls", "controls", 1500),
    Scene("alert-takeover", "oil", "street", 3000),
    Scene("workshop-card", "workshop", "street", 1500),
    Scene("self-test", "self-test", "street", 1500),
    Scene("day-street", "cruise", "street", 1500, "day"),
    Scene("g-meter", "gmeter", "gmeter", 1500, dashboard=G_METER_DASHBOARD),
    Scene("g-meter-locked", "cruise", "gmeter", 1500, dashboard=G_METER_DASHBOARD),
)


@dataclass
class Image:
    width: int
    height: int
    pixels: bytes


def bgra_to_rgb(row: bytes) -> bytes:
    rgb = bytearray(len(row) // 4 * RGB)
    rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
    return bytes(rgb)


def read_bmp(path: Path) -> Image:
    blob = path.read_bytes()
    offset = struct.unpack_from("<I", blob, 10)[0]
    width, height = struct.unpack_from("<ii", blob, 18)
    bpp = struct.unpack_from("<H", blob, 28)[0]
    if bpp != 32:
        raise VisualError(f"{path}: expected a 32-bit BMP, got {bpp}-bit")
    stride = width * 4
    rows = [
        bgra_to_rgb(blob[offset + y * stride : offset + (y + 1) * stride])
        for y in range(abs(height))
    ]
    ordered = rows if height < 0 else rows[::-1]
    return Image(width, abs(height), b"".join(ordered))


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


def write_png(path: Path, image: Image) -> None:
    stride = image.width * RGB
    raw = b"".join(
        b"\x00" + image.pixels[y * stride : (y + 1) * stride]
        for y in range(image.height)
    )
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        PNG_SIGNATURE
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )


def png_chunks(blob: bytes) -> dict[bytes, bytes]:
    chunks: dict[bytes, bytes] = {}
    position = len(PNG_SIGNATURE)
    while position < len(blob):
        length, kind = struct.unpack_from(">I4s", blob, position)
        chunks[kind] = (
            chunks.get(kind, b"") + blob[position + 8 : position + 8 + length]
        )
        position += 12 + length
    return chunks


def paeth(left: int, up: int, corner: int) -> int:
    estimate = left + up - corner
    distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
    return (left, up, corner)[distances.index(min(distances))]


def predictor(kind: int, left: int, up: int, corner: int) -> int:
    choices = {0: 0, 1: left, 2: up, 3: (left + up) // 2, 4: paeth(left, up, corner)}
    return choices[kind]


def unfilter_row(kind: int, row: bytes, previous: bytearray) -> bytearray:
    if kind == 0:
        return bytearray(row)
    out = bytearray(len(row))
    for i, value in enumerate(row):
        left = out[i - RGB] if i >= RGB else 0
        corner = previous[i - RGB] if i >= RGB else 0
        out[i] = (value + predictor(kind, left, previous[i], corner)) & 0xFF
    return out


def read_png(path: Path) -> Image:
    blob = path.read_bytes()
    chunks = png_chunks(blob)
    width, height, depth, color = struct.unpack_from(">IIBB", chunks[b"IHDR"])
    if (depth, color) != (8, 2):
        raise VisualError(
            f"{path}: expected 8-bit RGB, got depth {depth} color type {color}"
        )
    raw = zlib.decompress(chunks[b"IDAT"])
    stride = width * RGB
    previous, rows = bytearray(stride), []
    for y in range(height):
        start = y * (stride + 1)
        previous = unfilter_row(
            raw[start], raw[start + 1 : start + 1 + stride], previous
        )
        rows.append(bytes(previous))
    return Image(width, height, b"".join(rows))


def diff_image(golden: Image, actual: Image) -> Image:
    out = bytearray()
    for i in range(0, len(golden.pixels), RGB):
        same = golden.pixels[i : i + RGB] == actual.pixels[i : i + RGB]
        out += (
            bytes(c // DIFF_DIM for c in golden.pixels[i : i + RGB])
            if same
            else bytes(DIFF_MARK)
        )
    return Image(golden.width, golden.height, bytes(out))


def changed_pixels(golden: Image, actual: Image) -> int:
    if golden.pixels == actual.pixels:
        return 0
    pairs = zip(range(0, len(golden.pixels), RGB), range(0, len(actual.pixels), RGB))
    return sum(
        1 for g, a in pairs if golden.pixels[g : g + RGB] != actual.pixels[a : a + RGB]
    )


def capture(scene: Scene, tier: str, program: Path) -> Image:
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy"}
    environment.pop("CANSHIFT_SIM_BOARD", None)
    environment.pop("CANSHIFT_SIM_DASHBOARD", None)
    if scene.dashboard is not None:
        environment["CANSHIFT_SIM_DASHBOARD"] = str(scene.dashboard.resolve())
    if TIERS[tier] is not None:
        environment["CANSHIFT_SIM_BOARD"] = str(TIERS[tier].resolve())
    if scene.theme is not None:
        environment["CANSHIFT_SIM_THEME"] = scene.theme
    with tempfile.TemporaryDirectory() as scratch:
        command = [
            str(program.resolve()),
            str(DATA.resolve()),
            scene.scenario,
            scene.page,
            str(scene.capture_ms),
        ]
        result = subprocess.run(
            command, cwd=scratch, env=environment, capture_output=True, text=True
        )
        if result.returncode != 0:
            raise VisualError(
                f"{scene.name} at {tier}: sim exited {result.returncode}: {result.stderr.strip()[-400:]}"
            )
        return read_bmp(Path(scratch) / CAPTURE)


def compare(golden_path: Path, actual: Image) -> str:
    if not golden_path.exists():
        return "missing"
    golden = read_png(golden_path)
    if (golden.width, golden.height) != (actual.width, actual.height):
        return f"size {golden.width}x{golden.height} -> {actual.width}x{actual.height}"
    changed = changed_pixels(golden, actual)
    return "pass" if changed == 0 else f"{changed} px differ"


def keep_failure(failures: Path, key: str, golden_path: Path, actual: Image) -> None:
    write_png(failures / f"{key}.actual.png", actual)
    if not golden_path.exists():
        return
    golden = read_png(golden_path)
    if (golden.width, golden.height) == (actual.width, actual.height):
        write_png(failures / f"{key}.diff.png", diff_image(golden, actual))


def check_scene(args: argparse.Namespace, scene: Scene, tier: str) -> dict:
    key = f"{tier}/{scene.name}"
    golden_path = GOLDENS / f"{key}.png"
    actual = capture(scene, tier, args.program)
    if args.update_goldens:
        write_png(golden_path, actual)
        return {"name": key, "status": "updated"}
    status = compare(golden_path, actual)
    if status != "pass" and args.failures is not None:
        keep_failure(args.failures, key.replace("/", "__"), golden_path, actual)
    return {"name": key, "status": status}


def format_table(results: list[dict]) -> str:
    passed = sum(1 for result in results if result["status"] in ("pass", "updated"))
    lines = [
        f"### Visual goldens: {passed}/{len(results)} pass",
        "",
        "| frame | result |",
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
        passed = sum(1 for result in results if result["status"] in ("pass", "updated"))
        report = {"passed": passed, "failed": len(results) - passed, "cases": results}
        args.report.write_text(json.dumps(report, indent=2) + "\n")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="bench_visual.py")
    parser.add_argument("--program", type=Path, default=PROGRAM)
    parser.add_argument("--update-goldens", action="store_true")
    parser.add_argument("--failures", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--tier", choices=sorted(TIERS), action="append")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    tiers = args.tier or list(TIERS)
    try:
        results = [check_scene(args, scene, tier) for tier in tiers for scene in SCENES]
    except (OSError, VisualError, KeyError, zlib.error, struct.error) as error:
        sys.stderr.write(f"[visual] {error}\n")
        return 1
    write_outputs(args, results)
    return (
        0 if all(result["status"] in ("pass", "updated") for result in results) else 1
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
