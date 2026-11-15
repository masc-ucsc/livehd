# SPDX-License-Identifier: BSD-3-Clause
"""Strict little-endian raw map files (labels.bin, layer_groups.bin).

Layout: 8-byte magic, five u32 (format version 1, channels, bins_x, bins_y,
normalized flag), one f64 (min, max) pair per channel, then a channel/x/y f32
payload (the placer uses x-major bins). The magic is the one the first label
corpus was written with; it is kept so those files stay readable.
"""
from dataclasses import dataclass
import math
from pathlib import Path
import struct

HEADER = struct.Struct("<8s5I")
MAGIC = b"UROUTEM1"


@dataclass
class Maps:
    channels: int
    bins_x: int
    bins_y: int
    normalized: bool
    values: list
    ranges: list


def grid(x, y):
    if not (isinstance(x, int) and isinstance(y, int) and 1 <= x <= 1024 and 1 <= y <= 1024):
        raise ValueError("map grid dimensions must be in 1..1024")


def resample(values, fx, fy, tx, ty):
    """Conservative area average on coincident grids, in fixed x/y order."""
    grid(fx, fy)
    grid(tx, ty)
    if len(values) != fx * fy or not all(map(math.isfinite, values)):
        raise ValueError("invalid map resample input")
    if (fx, fy) == (tx, ty):
        return list(values)
    output = [0.] * (tx * ty)
    for x in range(tx):
        x0, x1 = x * fx // tx, ((x + 1) * fx + tx - 1) // tx
        for y in range(ty):
            y0, y1 = y * fy // ty, ((y + 1) * fy + ty - 1) // ty
            total = 0.
            for i in range(x0, x1):
                a = min((x + 1) * fx, (i + 1) * tx) - max(x * fx, i * tx)
                for j in range(y0, y1):
                    b = min((y + 1) * fy, (j + 1) * ty) - max(y * fy, j * ty)
                    total += values[i * fy + j] * (float(a) * b)
            output[x * ty + y] = total / (float(fx) * fy)
    return output


def validate(m):
    grid(m.bins_x, m.bins_y)
    if not 1 <= m.channels <= 16 or len(m.ranges) != m.channels:
        raise ValueError("invalid map channel count")
    area = m.bins_x * m.bins_y
    if len(m.values) != area * m.channels:
        raise ValueError("map shape mismatch")
    for c, (lo, hi) in enumerate(m.ranges):
        if not math.isfinite(lo) or not math.isfinite(hi) or hi < lo or not math.isfinite(hi - lo):
            raise ValueError("invalid map normalization range")
        # The payload is f32; a raw extremum can round outside its f64 range.
        low32, high32 = struct.unpack("<2f", struct.pack("<2f", lo, hi)) if not m.normalized else (0., 1.)
        for v in m.values[c * area:(c + 1) * area]:
            if not math.isfinite(v) or not low32 <= v <= high32 or (m.normalized and lo == hi and v != 0):
                raise ValueError("invalid map value")


def raw(channels, nx, ny):
    """Unnormalized maps from per-channel value lists; ranges are their extrema."""
    return Maps(len(channels), nx, ny, False, [v for c in channels for v in c], [(min(c), max(c)) for c in channels])


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
        raise ValueError("truncated map file")
    magic, version, channels, nx, ny, normalized = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1:
        raise ValueError("unsupported map file format")
    grid(nx, ny)
    if not 1 <= channels <= 16 or normalized not in (0, 1):
        raise ValueError("invalid map file header")
    count = channels * nx * ny
    offset = HEADER.size + channels * 16
    if len(data) != offset + count * 4:
        raise ValueError("map file length mismatch")
    ranges = [struct.unpack_from("<2d", data, HEADER.size + c * 16) for c in range(channels)]
    values = list(struct.unpack_from(f"<{count}f", data, offset))
    result = Maps(channels, nx, ny, bool(normalized), values, ranges)
    validate(result)
    return result


def read(path):
    path = Path(path)
    # Bound reads before allocation, including an adversarial header/file size.
    if path.stat().st_size > HEADER.size + 16 * 16 + 16 * 1024 * 1024 * 4:
        raise ValueError("oversized map file")
    return decode(path.read_bytes())


def write(path, maps):
    Path(path).write_bytes(encode(maps))
