# SPDX-License-Identifier: BSD-3-Clause
"""Retain a FastRoute 2D congestion-marker checkpoint as diagnostic maps.

These are lower bounds from emitted router markers, not complete overflow,
detailed-wire utilization, per-layer OpenDB resources, or training labels.
Reports may be capped/shuffled. Empty reports prove nothing.
"""
import argparse
import json
import math
from pathlib import Path
import re
import time

from .features import NETWORK_BINS, grid
from .maps import raw, write
from .pool import canonical, sha

NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
BOX = re.compile(rf'bbox\s*=\s*\(({NUMBER}),\s*({NUMBER})\)\s*-\s*\(({NUMBER}),\s*({NUMBER})\)\s+on Layer -')
COUNTS = re.compile(r'comment:\s*capacity:(\d+)\s+usage:(\d+)\s+congestion:(\d+)')


def marker_map(text, core, dbu, bins=NETWORK_BINS):
    grid(bins, bins)
    if (len(core) != 4 or not all(math.isfinite(v) for v in core) or
            not math.isfinite(dbu) or dbu <= 0):
        raise ValueError('invalid global-route diagnostic frame')
    lx, ly, hx, hy = core
    dx, dy = (hx - lx) / bins, (hy - ly) / bins
    if min(dx, dy) <= 0 or not math.isfinite(dx * dy):
        raise ValueError('invalid global-route diagnostic core')
    values, seen = [0.] * (bins * bins), set()
    counts = {'Horizontal congestion': 0, 'Vertical congestion': 0}
    sums = {'capacity': 0, 'usage': 0, 'overflow': 0}
    state = None

    def finish():
        if state is None:
            return
        if set(state) != {'direction', 'counts', 'box', 'sources'}:
            raise ValueError('incomplete global-route congestion marker')
        direction, (capacity, usage, overflow), box = state['direction'], state['counts'], state['box']
        if max(capacity, usage, overflow) > 2**53 or overflow <= 0 or usage - capacity != overflow:
            raise ValueError('inconsistent global-route demand counters')
        xl, yl, xh, yh = [v * dbu for v in box]
        if not all(math.isfinite(v) for v in (xl, yl, xh, yh)) or xh <= xl or yh <= yl:
            raise ValueError('invalid global-route marker box')
        key = (direction, xl, yl, xh, yh)
        if key in seen:
            raise ValueError('duplicate directional global-route marker')
        seen.add(key)
        counts[direction] += 1
        for name, value in zip(sums, (capacity, usage, overflow)):
            sums[name] += value
        ax, ay, az, aw = max(lx, xl), max(ly, yl), min(hx, xh), min(hy, yh)
        if ax >= az or ay >= aw:
            return
        for x in range(max(0, math.floor((ax - lx) / dx)), min(bins, math.ceil((az - lx) / dx))):
            ox = max(0., min(az, lx + (x + 1) * dx) - max(ax, lx + x * dx))
            for y in range(max(0, math.floor((ay - ly) / dy)), min(bins, math.ceil((aw - ly) / dy))):
                oy = max(0., min(aw, ly + (y + 1) * dy) - max(ay, ly + y * dy))
                values[x * bins + y] += overflow * (ox / dx) * (oy / dy)

    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('violation type: '):
            finish()
            direction = line.removeprefix('violation type: ')
            if direction not in counts:
                raise ValueError('report is not a FastRoute aggregate 2D congestion report')
            state = {'direction': direction}
            continue
        if state is None:
            raise ValueError('unexpected data before global-route marker')
        if line.startswith('srcs:'):
            name, value = 'sources', True
        elif match := COUNTS.fullmatch(line):
            name, value = 'counts', tuple(map(int, match.groups()))
        elif match := BOX.fullmatch(line):
            name, value = 'box', tuple(map(float, match.groups()))
        else:
            raise ValueError('unsupported global-route marker syntax')
        if name in state:
            raise ValueError('duplicate global-route marker field')
        state[name] = value
    finish()
    if not seen:
        raise ValueError('empty marker report does not establish a measured zero-overflow map')
    if not all(math.isfinite(v) for v in values):
        raise ValueError('global-route diagnostic accumulation overflow')
    return raw([values], bins, bins), {
        'source': 'FastRoute aggregate 2D congestion marker report', 'core': list(core), 'dbu_per_micron': dbu,
        'directional_marker_counts': counts, 'emitted_counter_sums': sums,
        'units': 'emitted usage-minus-capacity counters; both directions summed and area-averaged over marker rectangles',
        'net_scope': 'all nets represented by the router report; no feature data/control mask is applied',
        'coverage': 'potentially capped/shuffled positive markers; unlisted cells unknown, not measured zero',
        'map_role': 'lower bound from emitted marker footprints only; report completeness is not encoded',
        'directions_with_at_least_10000_markers': [name for name, count in counts.items() if count >= 10000],
        'limitations': 'not per-layer resource overflow, physical detailed-wire utilization, DRC labels, or routing-success evidence'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--frame', required=True, type=Path, help='sample/export JSON with core and dbu_per_micron')
    parser.add_argument('--output', required=True, type=Path, help='new snapshot directory; never overwritten')
    args = parser.parse_args()
    try:
        if args.report.stat().st_size > 64 * 1024 * 1024 or args.frame.stat().st_size > 1024 * 1024:
            raise ValueError('oversized diagnostic input')
        before = args.report.stat()
        raw, frame_raw = args.report.read_bytes(), args.frame.read_bytes()
        after = args.report.stat()
        if (before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_ino, after.st_size, after.st_mtime_ns):
            raise ValueError('congestion report changed while reading; retry after its writer closes it')
        frame = json.loads(frame_raw)
        maps, metadata = marker_map(raw.decode(), frame['core'], frame['dbu_per_micron'])
        metadata.update(report_filename=args.report.name, report_sha256=sha(raw), frame_sha256=sha(frame_raw),
                        report_bytes=len(raw), report_mtime_ns=after.st_mtime_ns, snapshot_created_at=time.time_ns(),
                        snapshot_version=1, purpose='intermediate/unrouted diagnostics only; never training labels',
                        chronology='filename iteration tags may restart; no monotonic iteration chronology is inferred')
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / 'source_report.rpt').write_bytes(raw)
        (args.output / 'frame.json').write_bytes(frame_raw)
        map_path = args.output / 'emitted_overflow_lower_bound.bin'
        write(map_path, maps)
        metadata['map_file'] = map_path.name
        metadata['map_sha256'] = sha(map_path.read_bytes())
        (args.output / 'metadata.json').write_bytes(canonical(metadata))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, f'uroute overflow: {error}\n')
    print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
