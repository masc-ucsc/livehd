# SPDX-License-Identifier: BSD-3-Clause
"""Router-reference tile scores; constants have explicitly undefined correlation.

python -m tools.uroute.score --reference labels.bin --prediction map.bin
RUDY TSV exports from tools/rudy_metrics are accepted with --prediction-tsv.
"""
import argparse
import csv
import json
import math
from pathlib import Path

from .features import grid, resample
from .maps import read


def pearson(a, b):
    ma, mb = math.fsum(a) / len(a), math.fsum(b) / len(b)
    aa, bb = [v - ma for v in a], [v - mb for v in b]
    va, vb = math.fsum(v * v for v in aa), math.fsum(v * v for v in bb)
    if va == 0 or vb == 0:
        return None
    return max(-1., min(1., math.fsum(x * y for x, y in zip(aa, bb)) / math.sqrt(va * vb)))


def ranks(values):
    order = sorted(range(len(values)), key=values.__getitem__)
    result, start = [0.] * len(values), 0
    while start < len(values):
        end = start + 1
        while end < len(values) and values[order[end]] == values[order[start]]:
            end += 1
        for i in range(start, end):
            result[order[i]] = (start + end - 1) / 2
        start = end
    return result


def ssim(a, b, nx, ny, scale):
    # Wang et al. local Gaussian SSIM: 11x11, sigma=1.5, population moments.
    # Clamp edge coordinates. Constants use a unit dynamic range.
    kernel = [math.exp(-i * i / 4.5) for i in range(-5, 6)]
    total = sum(kernel)
    kernel = [v / total for v in kernel]

    def blur(values):
        temp, out = [0.] * len(values), [0.] * len(values)
        for x in range(nx):
            for y in range(ny):
                temp[x * ny + y] = sum(k * values[x * ny + min(ny - 1, max(0, y + j - 5))]
                                        for j, k in enumerate(kernel))
        for x in range(nx):
            for y in range(ny):
                out[x * ny + y] = sum(k * temp[min(nx - 1, max(0, x + j - 5)) * ny + y]
                                       for j, k in enumerate(kernel))
        return out

    ma, mb = blur(a), blur(b)
    aa, bb, ab = blur([v * v for v in a]), blur([v * v for v in b]), blur([x * y for x, y in zip(a, b)])
    c1, c2 = (.01 * scale) ** 2, (.03 * scale) ** 2
    scores = []
    for x, y, xx, yy, xy in zip(ma, mb, aa, bb, ab):
        va, vb, covariance = max(0., xx - x * x), max(0., yy - y * y), xy - x * y
        scores.append(((2 * x * y + c1) * (2 * covariance + c2)) /
                      ((x * x + y * y + c1) * (va + vb + c2)))
    return math.fsum(scores) / len(scores)


def metrics(reference, prediction, nx, ny):
    if len(reference) != nx * ny or len(prediction) != nx * ny or nx < 1 or ny < 1:
        raise ValueError("score shape mismatch")
    if not all(math.isfinite(v) for v in (*reference, *prediction)):
        raise ValueError("nonfinite score input")
    extent = max(reference) - min(reference)
    rmse = math.sqrt(math.fsum((a - b) ** 2 for a, b in zip(reference, prediction)) / len(reference))
    # Tied hotspots receive fractional membership at the cutoff. A flat map
    # has 5% overlap with any other map, rather than an arbitrary index bias.
    count = max(1, math.ceil(len(reference) * .05))

    def membership(values):
        cutoff = sorted(values, reverse=True)[count - 1]
        above = sum(v > cutoff for v in values)
        equal = sum(v == cutoff for v in values)
        fraction = (count - above) / equal
        return [1. if v > cutoff else fraction if v == cutoff else 0. for v in values]

    ar, br = membership(reference), membership(prediction)
    return {"pearson": pearson(reference, prediction), "spearman": pearson(ranks(reference), ranks(prediction)),
            "ssim": ssim(reference, prediction, nx, ny, extent or 1.),
            "nrms": rmse / extent if extent else (0. if rmse == 0 else None), "rmse": rmse,
            "hotspot_top5_overlap": math.fsum(a * b for a, b in zip(ar, br)) / count,
            "tiles": len(reference), "reference_range": extent}


def absolute_channel(m, channel):
    if not 0 <= channel < m.channels:
        raise ValueError("missing score channel")
    area = m.bins_x * m.bins_y
    values = m.values[channel * area:(channel + 1) * area]
    if m.normalized:
        lo, hi = m.ranges[channel]
        return [lo + v * (hi - lo) for v in values]
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    predictions = parser.add_mutually_exclusive_group(required=True)
    predictions.add_argument("--prediction", type=Path)
    predictions.add_argument("--prediction-tsv", type=Path)
    parser.add_argument("--channel", type=int, default=0)
    args = parser.parse_args()
    ref = read(args.reference)
    if args.prediction:
        pred = read(args.prediction)
        values = resample(absolute_channel(pred, args.channel), pred.bins_x, pred.bins_y, ref.bins_x, ref.bins_y)
    else:
        with args.prediction_tsv.open() as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        if not rows:
            raise ValueError("empty RUDY TSV grid")
        nx, ny = max(int(r["x"]) for r in rows) + 1, max(int(r["y"]) for r in rows) + 1
        grid(nx, ny)
        data, seen = [0.] * (nx * ny), set()
        for row in rows:
            x, y = int(row["x"]), int(row["y"])
            if not (0 <= x < nx and 0 <= y < ny) or (x, y) in seen:
                raise ValueError("invalid/duplicate RUDY TSV tile")
            seen.add((x, y))
            # Routing-wire labels do not contain the separate pin-access term.
            data[x * ny + y] = max(float(row["horizontal"]), float(row["vertical"]))
        if len(seen) != nx * ny:
            raise ValueError("incomplete RUDY TSV grid")
        values = resample(data, nx, ny, ref.bins_x, ref.bins_y)
    print(json.dumps(metrics(absolute_channel(ref, args.channel), values, ref.bins_x, ref.bins_y), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
