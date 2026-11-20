# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Compare directional demand maps with routed metal length at one spatial resolution."""

import math
from statistics import StatisticsError, correlation


def compare_segments(path, floorplan, local, boundary=None):
    if not floorplan:
        raise ValueError("this export has no floorplan routing estimate")
    bins = 32
    origin = floorplan["frame_dbu"][:2]
    step = [(floorplan["frame_dbu"][axis + 2] - origin[axis]) / bins for axis in (0, 1)]

    def new_map():
        return [[0.0] * (bins * bins) for _ in range(2)]

    def index(coord, axis):
        return min(bins - 1, max(0, math.floor((coord - origin[axis]) / step[axis])))

    def overlaps(lo, hi, axis):
        for i in range(index(lo, axis), index(hi, axis) + 1):
            start = origin[axis] + i * step[axis]
            yield i, max(0.0, min(hi, start + step[axis]) - max(lo, start))

    measured = new_map()
    with path.open() as segments:
        for line in segments:
            fields = line.split()
            if len(fields) != 6 or fields[2] != fields[5]:
                continue
            x, y = int(fields[0]), int(fields[1])
            u, v = int(fields[3]), int(fields[4])
            if x == u and y == v:
                continue
            if x != u and y != v:
                raise ValueError("non-rectilinear global route segment")
            axis = int(x == u)
            fixed = index(x if axis else y, 1 - axis)
            lo, hi = sorted((y, v) if axis else (x, u))
            for i, length in overlaps(lo, hi, axis):
                measured[axis][i * bins + fixed if axis else fixed * bins + i] += length

    def projected(grids):
        out = new_map()
        for grid in grids:
            bounds = grid["frame_dbu"]

            def edges(k, n, grid=grid, bounds=bounds):
                return grid.get(("x_edges", "y_edges")[k]) or [
                    bounds[k] + (bounds[k + 2] - bounds[k]) * i / n
                    for i in range(n + 1)
                ]

            nx, ny = grid["columns"], grid["rows"]
            xs, ys = edges(0, nx), edges(1, ny)
            for i, tile in enumerate(grid["tiles"]):
                x, y = i % nx, i // nx
                scale = grid["pitch_dbu"] / ((xs[x + 1] - xs[x]) * (ys[y + 1] - ys[y]))
                for a, w in overlaps(xs[x], xs[x + 1], 0):
                    for b, h in overlaps(ys[y], ys[y + 1], 1):
                        for axis in (0, 1):
                            out[axis][b * bins + a] += (
                                (tile[axis] + tile[2 + axis]) * scale * w * h
                            )
        return out

    def stats(a, b):
        count = max(1, len(a) // 10)

        def top(v):
            return set(sorted(range(len(v)), key=v.__getitem__, reverse=True)[:count])

        try:
            r = correlation(a, b)
        except StatisticsError:
            r = None
        return {
            "pearson": r,
            "top_decile_recall": len(top(a) & top(b)) / count
            if r is not None
            else None,
        }

    output = {}
    for name, grids in [
        ("floorplan", [floorplan]),
        ("boundary_contacts", [boundary] if boundary else []),
        ("placed_homes", local),
    ]:
        if grids:
            estimated = projected(grids)
            output[name] = {
                direction: stats(measured[k], estimated[k])
                for k, direction in enumerate(("horizontal", "vertical"))
            }
    return {
        "bins": bins,
        "reference": "all-net same-layer routed metal length within the floorplan",
        "models": output,
        "metal_length_dbu": {
            "columns": bins,
            "rows": bins,
            "frame_dbu": floorplan["frame_dbu"],
            "tiles": list(zip(*measured)),
        },
    }
