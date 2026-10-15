import os
import struct
import sys
import zlib

Import("env")  # noqa: F821

DEFLATED_DIR = os.path.join(".pio", "deflated")
LENGTH_HEADER = "<I"
COMPRESSION_LEVEL = 9
WINDOW_BITS = 12
MEMORY_LEVEL = 9


def fail(message):
    sys.stderr.write(f"[deflate] ERROR: {message}\n")
    sys.exit(1)


def embedded_entries():
    lines = env.GetProjectOption("board_build.embed_files", "").splitlines()  # noqa: F821
    return [line.strip() for line in lines if line.strip()]


def source_for(entry):
    prefix = DEFLATED_DIR + os.sep
    if not entry.startswith(prefix):
        fail(f"{entry} is embedded raw; list it under {prefix}")
    return entry[len(prefix) :]


def pack(raw, source):
    if not raw:
        fail(f"{source} is empty")
    compressor = zlib.compressobj(
        COMPRESSION_LEVEL, zlib.DEFLATED, WINDOW_BITS, MEMORY_LEVEL
    )
    stream = compressor.compress(raw) + compressor.flush()
    if zlib.decompress(stream) != raw:
        fail(f"{source} does not survive a deflate round trip")
    return struct.pack(LENGTH_HEADER, len(raw)) + stream


def write_if_changed(path, content):
    if os.path.exists(path):
        with open(path, "rb") as handle:
            if handle.read() == content:
                return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(content)


def deflate(project_dir, entry):
    source = source_for(entry)
    with open(os.path.join(project_dir, source), "rb") as handle:
        raw = handle.read()
    packed = pack(raw, source)
    write_if_changed(os.path.join(project_dir, entry), packed)
    print(f"[deflate] {source}: {len(raw)} -> {len(packed)} bytes")


for embedded in embedded_entries():
    deflate(env["PROJECT_DIR"], embedded)  # noqa: F821
