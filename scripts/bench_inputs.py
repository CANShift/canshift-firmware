import base64
import hashlib
import json
import sys
from pathlib import Path

BUNDLED_SIGNALS = Path("data/config/signals.json")
CAN_GOLDENS = Path("bench/goldens/can")
PROTOCOL_GOLDENS = Path("bench/goldens/protocol")
FRAME_BYTES = 8
DEMO_PERIOD_MS = 50
DEMO_LIVE_UNTIL_MS = 1500
DEMO_RESUME_MS = 2600
DEMO_END_MS = 8000
OBD_RESPONSE_ID = 0x7E8
OBD_PERIOD_MS = 100
OBD_LIVE_UNTIL_MS = 1200
OBD_END_MS = 4000

OTA_IMAGE_BYTES = 96
OTA_CHUNK_BYTES = 48
OTA_DESC_OFFSET = 0x20
OTA_APP_DESC_MAGIC = 0xABCD5432
OTA_FOREIGN_SIGNATURE = bytes([0x11] * 63 + [0x00])
SMALL_DASHBOARD = '{"version":"1.0.0","pages":[{"id":"bench","widgets":[]}]}'
CONFIG_CHUNK_BYTES = 24

OBD_PIDS = (
    ("rpm", 0x0C, 2, 0.25, 0.0, 800.0, 6400.0, "rpm"),
    ("speed_kph", 0x0D, 1, 1.0, 0.0, 0.0, 180.0, "km/h"),
    ("coolant_temp_c", 0x05, 1, 1.0, -40.0, 20.0, 105.0, "°C"),
    ("throttle_pos", 0x11, 1, 100 / 255, 0.0, 0.0, 100.0, "%"),
    ("iat_c", 0x0F, 1, 1.0, -40.0, 10.0, 60.0, "°C"),
    ("battery_volts", 0x42, 2, 0.001, 0.0, 12.0, 14.4, "V"),
)


def triangle(at_ms: int, period_ms: int) -> float:
    phase = (at_ms % (2 * period_ms)) / period_ms
    return phase if phase < 1 else 2 - phase


def to_raw(signal: dict, physical: float) -> int:
    raw = round((physical - signal.get("offset", 0.0)) / signal.get("scale", 1.0))
    bits = 8 * signal["byteLength"]
    return raw & ((1 << bits) - 1)


def place(payload: bytearray, signal: dict, raw: int) -> None:
    start, length = signal["startByte"], signal["byteLength"]
    order = "big" if signal.get("bigEndian", True) else "little"
    payload[start : start + length] = raw.to_bytes(length, order)


def flag_bit(signal: dict, at_ms: int, index: int) -> int:
    mask = int(signal["bitMask"], 16)
    return mask if (at_ms // (100 * (index + 1))) % 2 else 0


def physical_of(signal: dict, at_ms: int, index: int) -> float:
    span = signal["max"] - signal["min"]
    return signal["min"] + span * triangle(at_ms, 400 + 37 * index)


def demo_payload(signals: list[dict], at_ms: int) -> bytearray:
    payload = bytearray(FRAME_BYTES)
    for index, signal in enumerate(signals):
        if "bitMask" in signal:
            payload[signal["startByte"]] |= flag_bit(signal, at_ms, index)
            continue
        place(payload, signal, to_raw(signal, physical_of(signal, at_ms, index)))
    return payload


def frames_by_id(signals: list[dict]) -> dict[int, list[dict]]:
    grouped: dict[int, list[dict]] = {}
    for signal in signals:
        grouped.setdefault(int(signal["canFrameId"], 16), []).append(signal)
    return grouped


def trace_line(at_ms: int, frame_id: int, payload: bytes) -> str:
    return f"{at_ms} rx 0x{frame_id:03X} {payload.hex().upper()}".rstrip()


def demo_cycle(grouped: dict[int, list[dict]], at_ms: int) -> list[str]:
    return [
        trace_line(at_ms + slot, fid, demo_payload(sigs, at_ms))
        for slot, (fid, sigs) in enumerate(sorted(grouped.items()))
    ]


def demo_edges(grouped: dict[int, list[dict]], at_ms: int) -> list[str]:
    first_id = min(grouped)
    full = demo_payload(grouped[first_id], at_ms)
    return [
        trace_line(at_ms, first_id, full[:3]),
        trace_line(at_ms + 1, 0x123, bytes(range(FRAME_BYTES))),
        trace_line(at_ms + 2, first_id, b""),
    ]


def demo_trace() -> list[str]:
    grouped = frames_by_id(json.loads(BUNDLED_SIGNALS.read_text())["signals"])
    lines: list[str] = []
    for at_ms in range(0, DEMO_LIVE_UNTIL_MS, DEMO_PERIOD_MS):
        lines += demo_cycle(grouped, at_ms)
    lines += demo_edges(grouped, DEMO_LIVE_UNTIL_MS)
    lines += demo_cycle(grouped, DEMO_RESUME_MS)
    return lines + [f"{DEMO_END_MS} end"]


def obd_signal(
    name: str,
    pid: int,
    length: int,
    scale: float,
    offset: float,
    low: float,
    high: float,
    unit: str,
) -> dict:
    return {
        "name": name,
        "canFrameId": f"0x{OBD_RESPONSE_ID:03X}",
        "startByte": 3,
        "byteLength": length,
        "bigEndian": True,
        "signed": False,
        "scale": scale,
        "offset": offset,
        "unit": unit,
        "min": low,
        "max": high,
        "timeoutMs": 1000,
        "polling": {"mode": 1, "pid": pid, "intervalMs": OBD_PERIOD_MS},
    }


def obd_config() -> dict:
    signals = [obd_signal(*pid) for pid in OBD_PIDS]
    return {
        "version": "1.0.0",
        "protocol": "obd2_j1979",
        "canSpeedKbps": 500,
        "signals": signals,
    }


def obd_response(signal: dict, pid: int, at_ms: int, index: int) -> bytes:
    raw = to_raw(signal, physical_of(signal, at_ms, index))
    data = bytes([2 + signal["byteLength"], 0x41, pid]) + raw.to_bytes(
        signal["byteLength"], "big"
    )
    return data.ljust(FRAME_BYTES, b"\x00")


def obd_cycle(signals: list[dict], at_ms: int) -> list[str]:
    lines = []
    for index, (signal, pid) in enumerate(zip(signals, (p[1] for p in OBD_PIDS))):
        lines.append(
            trace_line(
                at_ms + 5 + 10 * index,
                OBD_RESPONSE_ID,
                obd_response(signal, pid, at_ms, index),
            )
        )
    return lines


def obd_edges(at_ms: int) -> list[str]:
    return [
        trace_line(at_ms, OBD_RESPONSE_ID, bytes([0x03, 0x7F, 0x01, 0x12, 0, 0, 0, 0])),
        trace_line(at_ms + 1, OBD_RESPONSE_ID, bytes([0x04, 0x41, 0x0C, 0x1A])),
        trace_line(
            at_ms + 2, OBD_RESPONSE_ID, bytes([0x03, 0x41, 0x99, 0x10, 0, 0, 0, 0])
        ),
        trace_line(at_ms + 3, 0x7E9, bytes([0x03, 0x41, 0x0D, 0x50, 0, 0, 0, 0])),
    ]


def obd_trace(config: dict) -> list[str]:
    lines: list[str] = []
    for at_ms in range(0, OBD_LIVE_UNTIL_MS, OBD_PERIOD_MS):
        lines += obd_cycle(config["signals"], at_ms)
    lines += obd_edges(OBD_LIVE_UNTIL_MS)
    return lines + [f"{OBD_END_MS} end"]


def write_case(name: str, lines: list[str], config: dict | None) -> None:
    case = CAN_GOLDENS / name
    case.mkdir(parents=True, exist_ok=True)
    (case / "trace.txt").write_text("\n".join(lines) + "\n")
    if config is not None:
        (case / "signals.json").write_text(
            json.dumps(config, indent=2, ensure_ascii=False) + "\n"
        )


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def command(cmd: int, **fields) -> str:
    return json.dumps({"cmd": cmd, **fields}, separators=(",", ":"))


def ota_image(magic: int) -> bytes:
    image = bytearray(range(OTA_IMAGE_BYTES))
    image[0] = 0xE9
    image[OTA_DESC_OFFSET : OTA_DESC_OFFSET + 4] = magic.to_bytes(4, "little")
    return bytes(image)


def ota_begin(image: bytes, sha: str | None = None, **fields) -> str:
    digest = sha if sha is not None else hashlib.sha256(image).hexdigest()
    return command(
        0x30, total=len(image), sha256=digest, version="9.9.9-bench", **fields
    )


def ota_writes(image: bytes) -> list[str]:
    offsets = range(0, len(image), OTA_CHUNK_BYTES)
    return [
        command(0x31, offset=o, data=b64(image[o : o + OTA_CHUNK_BYTES]))
        for o in offsets
    ]


def ota_session_lines() -> list[str]:
    image = ota_image(OTA_APP_DESC_MAGIC)
    signed = {"sig": b64(OTA_FOREIGN_SIGNATURE)}
    writes = ota_writes(image)
    return [
        command(0x30, total=0, sha256="00"),
        ota_begin(image),
        ota_begin(image, sig="AAAA"),
        command(0x31, offset=0, data=b64(image[:OTA_CHUNK_BYTES])),
        ota_begin(image, **signed),
        command(0x31, offset=OTA_CHUNK_BYTES, data=b64(image[:OTA_CHUNK_BYTES])),
        command(0x31, offset=0, data="!!!!"),
        ota_begin(image, **signed),
        *writes,
        command(0x32, action="commit"),
    ]


def ota_failure_lines() -> list[str]:
    image = ota_image(OTA_APP_DESC_MAGIC)
    foreign = ota_image(0)
    signed = {"sig": b64(OTA_FOREIGN_SIGNATURE)}
    return [
        ota_begin(foreign, **signed),
        *ota_writes(foreign),
        ota_begin(image, sha="ff" * 32, **signed),
        *ota_writes(image),
        command(0x32, action="commit"),
        ota_begin(image, **signed),
        ota_writes(image)[0],
        command(0x32, action="commit"),
        ota_begin(image, **signed),
        command(0x32),
        command(0x32, action="commit"),
    ]


def config_chunks(text: str) -> list[str]:
    raw = text.encode()
    parts = [
        raw[i : i + CONFIG_CHUNK_BYTES] for i in range(0, len(raw), CONFIG_CHUNK_BYTES)
    ]
    head = [command(0x0E, total=len(parts), idx=0, bytes=len(raw), data=b64(parts[0]))]
    return head + [
        command(0x0E, total=len(parts), idx=i, data=b64(p))
        for i, p in enumerate(parts)
        if i > 0
    ]


def config_lines() -> list[str]:
    chunks = config_chunks(SMALL_DASHBOARD)
    return [
        command(0x0E, total=2, idx=0),
        command(0x0E, total=2, idx=1, data=b64(b"{}")),
        command(0x0E, total=2, idx=0, bytes=2, data="!!!!"),
        *chunks,
        command(0x01),
        command(0x03),
        command(0x0B),
        command(0x04),
        command(0x04, device_config={"units": "metric"}),
        command(0x03),
    ]


def status_lines() -> list[str]:
    return [
        command(0x10),
        command(0x11),
        command(0x09),
        command(0x09, day=False),
        command(0x07),
        command(0x08),
        command(0x0A),
        command(0x20),
        command(0x21),
        command(0xC8),
        '{"cmd":16,',
        "{}",
        command(0xF0),
    ]


def write_protocol(name: str, lines: list[str]) -> None:
    PROTOCOL_GOLDENS.mkdir(parents=True, exist_ok=True)
    (PROTOCOL_GOLDENS / f"{name}.in").write_text("\n".join(lines) + "\n")


def main() -> int:
    write_case("maxxecu_demo", demo_trace(), None)
    config = obd_config()
    write_case("obd2_j1979", obd_trace(config), config)
    write_protocol("status", status_lines())
    write_protocol("config", config_lines())
    write_protocol("ota_session", ota_session_lines())
    write_protocol("ota_failures", ota_failure_lines())
    return 0


if __name__ == "__main__":
    sys.exit(main())
