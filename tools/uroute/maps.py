# SPDX-License-Identifier: BSD-3-Clause
"""Strict little-endian map interchange. No Python-only serialization."""
import math
from pathlib import Path
import struct

from .features import Maps, grid

HEADER = struct.Struct("<8s5I")
MAGIC = b"UROUTEM1"


def validate(m):
    grid(m.bins_x, m.bins_y)
    if not 1 <= m.channels <= 16 or len(m.ranges) != m.channels:
        raise ValueError("invalid URoute channel count")
    area = m.bins_x * m.bins_y
    if len(m.values) != area * m.channels:
        raise ValueError("URoute map shape mismatch")
    for c, (lo, hi) in enumerate(m.ranges):
        if not math.isfinite(lo) or not math.isfinite(hi) or hi < lo or not math.isfinite(hi - lo):
            raise ValueError("invalid URoute normalization range")
        # The payload is f32; a raw extremum can round outside its f64 range.
        low32, high32 = struct.unpack("<2f", struct.pack("<2f", lo, hi)) if not m.normalized else (0., 1.)
        for v in m.values[c * area:(c + 1) * area]:
            if not math.isfinite(v) or not low32 <= v <= high32 or (m.normalized and lo == hi and v != 0):
                raise ValueError("invalid URoute map value")


def encode(m):
    # Round once at the interchange boundary, identically to native f32 output.
    payload = struct.pack(f"<{len(m.values)}f", *m.values)
    rounded = Maps(m.channels, m.bins_x, m.bins_y, m.normalized,
                   list(struct.unpack(f"<{len(m.values)}f", payload)), m.ranges)
    validate(rounded)
    header = HEADER.pack(MAGIC, 1, m.channels, m.bins_x, m.bins_y, int(m.normalized))
    return header + b"".join(struct.pack("<2d", *r) for r in m.ranges) + payload


def decode(data):
    if len(data) < HEADER.size:
        raise ValueError("truncated URoute map")
    magic, version, channels, nx, ny, normalized = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1:
        raise ValueError("unsupported URoute map format")
    grid(nx, ny)
    if not 1 <= channels <= 16 or normalized not in (0, 1):
        raise ValueError("invalid URoute map header")
    count = channels * nx * ny
    offset = HEADER.size + channels * 16
    if len(data) != offset + count * 4:
        raise ValueError("URoute map length mismatch")
    ranges = [struct.unpack_from("<2d", data, HEADER.size + c * 16) for c in range(channels)]
    values = list(struct.unpack_from(f"<{count}f", data, offset))
    result = Maps(channels, nx, ny, bool(normalized), values, ranges)
    validate(result)
    return result


def read(path):
    path = Path(path)
    # Bound reads before allocation, including an adversarial header/file size.
    if path.stat().st_size > HEADER.size + 16 * 16 + 16 * 1024 * 1024 * 4:
        raise ValueError("oversized URoute map")
    return decode(path.read_bytes())


def write(path, maps):
    Path(path).write_bytes(encode(maps))
