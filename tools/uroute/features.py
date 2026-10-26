# SPDX-License-Identifier: BSD-3-Clause
"""Version 1 physical feature reference, independent of NumPy and PyTorch.

Layout is channel/x/y (the placer uses x-major bins). The caller supplies physical
boxes and physical pin positions in one consistent unit. See README.md for the
boundary, degeneracy, resampling and normalization conventions.
"""
from dataclasses import dataclass
import math

FEATURE_VERSION = 1
LABEL_VERSION = 1
ARCH_VERSION = 1
NETWORK_BINS = 256  # architecture-v1 support/query grid, as native uroute_network_bins


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
        raise ValueError("URoute grid dimensions must be in 1..1024")


def resample(values, fx, fy, tx, ty):
    """Conservative area average on coincident grids, in fixed x/y order."""
    grid(fx, fy)
    grid(tx, ty)
    if len(values) != fx * fy or not all(map(math.isfinite, values)):
        raise ValueError("invalid URoute resample input")
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


def features(design, nx, ny, ox=NETWORK_BINS, oy=NETWORK_BINS):
    """design: core=[xl,yl,xh,yh], nodes=[dict], nets=[dict(pins, enabled)].

    Nodes: x,y,width,height, macro=False, mask=False, pad_x=pad_y=0.
    Nets contain absolute [x,y] pin pairs. Weights deliberately have no effect.
    """
    grid(nx, ny)
    grid(ox, oy)
    lx, ly, hx, hy = map(float, design["core"])
    if not all(map(math.isfinite, (lx, ly, hx, hy))) or hx <= lx or hy <= ly:
        raise ValueError("invalid URoute core")
    dx, dy = (hx - lx) / nx, (hy - ly) / ny
    if not math.isfinite(dx * dy) or dx * dy <= 0:
        raise ValueError("invalid URoute core area")
    raw = [[0.] * (nx * ny) for _ in range(4)]

    def bin_index(p, lo, step, count):
        return max(0, min(count - 1, math.floor((p - lo) / step)))

    def box(xl, yl, xh, yh):
        if not all(map(math.isfinite, (xl, yl, xh, yh))):
            raise ValueError("nonfinite URoute physical box")
        if xh <= lx or yh <= ly or xl >= hx or yl >= hy or xh <= xl or yh <= yl:
            return
        for x in range(bin_index(xl, lx, dx, nx), bin_index(xh, lx, dx, nx) + 1):
            a = max(0., min(xh, lx + (x + 1) * dx) - max(xl, lx + x * dx))
            for y in range(bin_index(yl, ly, dy, ny), bin_index(yh, ly, dy, ny) + 1):
                b = max(0., min(yh, ly + (y + 1) * dy) - max(yl, ly + y * dy))
                if a > 0 and b > 0:
                    yield x * ny + y, a * b

    for net in design["nets"]:
        pins = net["pins"]
        if not net.get("enabled", True) or len(pins) < 2:
            continue
        if not all(math.isfinite(p) for pin in pins for p in pin):
            raise ValueError("nonfinite URoute pin")
        xl, xh = min(p[0] for p in pins), max(p[0] for p in pins)
        yl, yh = min(p[1] for p in pins), max(p[1] for p in pins)
        w, h = xh - xl, yh - yl
        if not math.isfinite(w) or not math.isfinite(h):
            raise ValueError("URoute net bbox overflow")
        if w == 0 or h == 0:
            continue
        demand = 1 / w + 1 / h
        for i, area in box(xl, yl, xh, yh):
            raw[0][i] += demand * (area / (dx * dy))
        for x, y in pins:
            if lx <= x <= hx and ly <= y <= hy:
                raw[1][bin_index(x, lx, dx, nx) * ny + bin_index(y, ly, dy, ny)] += demand
    for node in design["nodes"]:
        if node.get("mask", False):
            continue
        px, py = node.get("pad_x", 0.), node.get("pad_y", 0.)
        xl, yl = node["x"] + px, node["y"] + py
        xh, yh = node["x"] + node["width"] - px, node["y"] + node["height"] - py
        for i, area in box(xl, yl, xh, yh):
            if node.get("macro", False):
                raw[2][i] = 1.
            else:
                raw[3][i] += area
    result = Maps(4, ox, oy, True, [], [])
    for channel in raw:
        values = resample(channel, nx, ny, ox, oy)
        lo, hi = min(values), max(values)
        if not math.isfinite(hi - lo):
            raise ValueError("URoute feature accumulation overflow")
        result.ranges.append((lo, hi))
        result.values.extend((v - lo) / (hi - lo) if hi != lo else 0. for v in values)
    return result
