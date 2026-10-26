# SPDX-License-Identifier: BSD-3-Clause
"""Actual routed-wire labels from OpenROAD's regular-net DEF and TR DRC report.

Geometry is produced by placement_metrics --uroute-geometry from this same DEF.
This parser deliberately rejects unsupported wire syntax rather than estimating
labels from a global-route map or silently skipping unknown path constructs.
"""
import argparse
from collections import defaultdict
import gzip
import json
import math
from pathlib import Path
import re

from .features import NETWORK_BINS, grid, resample
from .maps import raw, write

TOKEN = re.compile(r'\\[^\s]+|"(?:\\.|[^"\\])*"|[()+;]|[^\s()+;]+')
NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
BBOX = re.compile(rf'\s*bbox =\s*\(\s*({NUMBER}),\s*({NUMBER})\s*\)\s*-\s*'
                  rf'\(\s*({NUMBER}),\s*({NUMBER})\s*\) on Layer (\S+)\s*')


def text_file(path):
    path = Path(path)
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        return stream.read()


def routed_segments(text, layers, layer_widths=None, selected_nets=None):
    """Return net -> merged (layer,x1,y1,x2,y2) centerline segments in DEF DBU.

    Regular ROUTED/FIXED/COVER paths, NEW, wildcard/extended points, terminal
    vias and RECT patches are supported. A via followed by another point needs
    via-stack decoding and is rejected. RECT patches and point extensions enter
    the separate OpenDB-equivalent length census used for detour; patches have
    no track centerline. SPECIALNETS (power mesh) are outside this task.
    """
    section = re.search(r'^\s*NETS\s+(\d+)\s*;(.*?)^\s*END NETS\s*$', text, re.M | re.S)
    if not section:
        raise ValueError('missing regular NETS section')
    tokens = TOKEN.findall(re.sub(r'(?m)^\s*#.*$', '', section[2]))
    records, start = [], 0
    for i, token in enumerate(tokens):
        if token == ';':
            records.append(tokens[start:i])
            start = i + 1
    if start != len(tokens) or len(records) != int(section[1]):
        raise ValueError('DEF NETS count/record mismatch')
    result, counts = {}, {'vias': 0, 'rect_patches': 0, 'raw_segments': 0, 'net_shape_lengths': {}}
    layer_widths = layer_widths or {}
    for record in records:
        if len(record) < 2 or record[0] != '-' or record[1] in result:
            raise ValueError('invalid or duplicate DEF net')
        if selected_nets is not None and record[1] not in selected_nets:
            result[record[1]] = []
            counts['net_shape_lengths'][record[1]] = 0.
            continue
        lines = defaultdict(list)
        shape_length = 0.
        previous_extension = None
        i = 2
        while i < len(record):
            if record[i] == '+' and i + 1 < len(record) and record[i + 1] in ('SHIELD', 'NOSHIELD', 'VPIN', 'SUBNET', 'NONDEFAULTRULE'):
                raise ValueError('unsupported DEF shield/subnet routing')
            if record[i] != '+' or i + 1 == len(record) or record[i + 1] not in ('ROUTED', 'FIXED', 'COVER'):
                i += 1
                continue
            i += 2
            layer, previous, via = None, None, False
            while i < len(record) and record[i] != '+':
                token = record[i]
                if layer is None or token == 'NEW':
                    if token == 'NEW':
                        i += 1
                    if i == len(record) or record[i] not in layers:
                        raise ValueError('unknown DEF routing layer')
                    layer, previous, via = record[i], None, False
                    i += 1
                elif token in ('TAPER',):
                    i += 1
                elif token in ('TAPERRULE', 'STYLE'):
                    raise ValueError('non-default path widths/styles require rule decoding')
                elif token == 'MASK':
                    if i + 1 >= len(record):
                        raise ValueError('truncated DEF path attribute')
                    i += 2
                elif token == 'RECT':
                    if record[i + 1:i + 2] != ['('] or record[i + 6:i + 7] != [')']:
                        raise ValueError('invalid DEF RECT patch')
                    if not all(re.fullmatch(r'-?\d+', v) for v in record[i + 2:i + 6]):
                        raise ValueError('invalid DEF RECT coordinates')
                    xl, yl, xh, yh = map(int, record[i + 2:i + 6])
                    if xh < xl or yh < yl:
                        raise ValueError('inverted DEF RECT patch')
                    shape_length += abs((xh - xl) - (yh - yl))
                    counts['rect_patches'] += 1
                    i += 7
                elif token == '(':
                    if via:
                        raise ValueError('point after via requires via-stack decoding; export split NEW paths')
                    try:
                        end = record.index(')', i + 1)
                    except ValueError as error:
                        raise ValueError('unterminated DEF point') from error
                    point = record[i + 1:end]
                    if len(point) not in (2, 3) or any(v != '*' and not re.fullmatch(r'-?\d+', v) for v in point):
                        raise ValueError('invalid DEF routing point')
                    xy = []
                    for axis, value in enumerate(point[:2]):
                        if value == '*' and previous is None:
                            raise ValueError('wildcard in initial DEF point')
                        xy.append(previous[axis] if value == '*' else int(value))
                    extension = int(point[2]) if len(point) == 3 and point[2] != '*' else None
                    if len(point) == 3 and (extension is None or extension < 0):
                        raise ValueError('invalid DEF point extension')
                    if previous is not None and xy != previous:
                        x, y = previous
                        u, v = xy
                        if x != u and y != v:
                            raise ValueError('non-Manhattan DEF routing segment')
                        direction, fixed, lo, hi = ('h', y, min(x, u), max(x, u)) if y == v else ('v', x, min(y, v), max(y, v))
                        lines[layer, direction, fixed].append((lo, hi))
                        half_width = math.floor(layer_widths.get(layer, 0) / 2 + 1e-7)
                        a = half_width if previous_extension is None else previous_extension
                        b = half_width if extension is None else extension
                        shape_length += abs((hi - lo) + a + b - 2 * half_width)
                        counts['raw_segments'] += 1
                    previous, previous_extension, i = xy, extension, end + 1
                else:
                    # A via name is the only remaining legal token after a
                    # point. Terminal orientation is allowed after that via.
                    if previous is None or via or token in ('DO', 'BY', 'STEP', 'VIRTUAL', 'SHAPE') or re.fullmatch(r'-?\d+', token):
                        raise ValueError(f'unsupported DEF route token: {token}')
                    via = True
                    counts['vias'] += 1
                    i += 1
                    if i < len(record) and record[i] in ('N', 'S', 'E', 'W', 'FN', 'FS', 'FE', 'FW'):
                        i += 1
        merged = []
        for (layer, direction, fixed), spans in sorted(lines.items()):
            intervals = []
            for lo, hi in sorted(spans):
                if intervals and lo <= intervals[-1][1]:
                    intervals[-1][1] = max(hi, intervals[-1][1])
                else:
                    intervals.append([lo, hi])
            for lo, hi in intervals:
                merged.append((layer, lo, fixed, hi, fixed) if direction == 'h' else (layer, fixed, lo, fixed, hi))
        result[record[1]] = merged
        counts['net_shape_lengths'][record[1]] = shape_length
    return result, counts


def drc_markers(text, dbu):
    """OpenDB writeTR format: count each marker once at its bbox center."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    result, i = [], 0
    while i < len(lines):
        if not lines[i].startswith('violation type:') or i + 2 >= len(lines) or not lines[i + 1].startswith('srcs:'):
            raise ValueError('invalid/truncated TR DRC report')
        i += 2
        if lines[i].startswith(('comment:', 'congestion information:')):
            i += 1
        match = BBOX.fullmatch(lines[i]) if i < len(lines) else None
        if not match:
            raise ValueError('invalid DRC marker bbox')
        xl, yl, xh, yh = (float(v) * dbu for v in match.groups()[:4])
        if not all(map(math.isfinite, (xl, yl, xh, yh))) or xh < xl or yh < yl:
            raise ValueError('invalid DRC marker bounds')
        result.append(((xl + xh) / 2, (yl + yh) / 2))
        i += 1
    return result


def verify_lengths(report, text, dbu):
    """Compare enabled nets against OpenROAD report_wire_length -detailed_route.

    The report uses default stream precision, so permit its last printed digit
    plus one DBU. Missing/duplicate nets and larger differences are errors.
    """
    lengths = {}
    for line in text.splitlines():
        if not line.strip() or line.startswith('tool '):
            continue
        match = re.fullmatch(rf'drt:\s+(.+)\s+({NUMBER})\s+\d+\s*', line)
        if not match or match[1] in lengths:
            raise ValueError('invalid/duplicate OpenROAD detailed wire-length record')
        value = float(match[2]) * dbu
        if not math.isfinite(value) or value < 0:
            raise ValueError('invalid OpenROAD wire length')
        lengths[match[1]] = value
    maximum, total, odb_total = 0., 0., 0.
    for net in report['nets']:
        if net['net'] not in lengths:
            raise ValueError(f'missing OpenROAD wire-length net: {net["net"]}')
        expected, actual = lengths[net['net']], net['wirelength_dbu']
        difference = abs(expected - actual)
        if difference > max(1., 5e-6 * expected):
            raise ValueError(f'OpenROAD/DEF wire-length mismatch: {net["net"]}: {actual} vs {expected} DBU')
        maximum = max(maximum, difference)
        total += actual
        odb_total += expected
    return {'nets': len(report['nets']), 'max_abs_difference_dbu': maximum,
            'parsed_total_dbu': total, 'odb_total_dbu': odb_total}


def extract(geometry, definition, drc, flute_lengths, nx=64, ny=64, ox=NETWORK_BINS, oy=NETWORK_BINS):
    """Wire-length/nominal-track-length utilization and length-weighted detour.

    Group channels: M2-M3 H,V; M4-M5 H,V; M6+ H,V (LEF stack positions).
    Aggregate is max over all six channels. M1 local access contributes to
    detour but not these interconnect groups. Preferred-direction capacity is
    sum(1/pitch) within each group; wrong-way wires use that group's matching
    directional capacity. A nonzero demand with no capacity is an error.
    """
    grid(nx, ny)
    grid(ox, oy)
    route_grid = [nx, ny]
    # Labels measure wires and marker counts directly on the network grid.
    # Only input features originate on the placer route grid.
    nx, ny = ox, oy
    if geometry.get('geometry_version') != 1:
        raise ValueError('unsupported routed geometry version')
    lx, ly, hx, hy = geometry['core']
    if not all(map(math.isfinite, (lx, ly, hx, hy))) or hx <= lx or hy <= ly:
        raise ValueError('invalid routed core')
    dx, dy = (hx - lx) / nx, (hy - ly) / ny
    layers = geometry['layers']
    lookup = {layer['name']: i for i, layer in enumerate(layers)}
    if len(lookup) != len(layers) or len(layers) < 3:
        raise ValueError('need unique routing layers including M1-M3')
    widths = {layer['name']: layer['width'] for layer in layers}
    if not all(math.isfinite(v) and v > 0 for v in widths.values()):
        raise ValueError('invalid routing layer width')
    selected = {n['name'] for n in geometry['nets'] if n.get('enabled', True) and len(n['pins']) >= 2}
    routes, counts = routed_segments(definition, lookup, widths, selected)
    dbu_match = re.search(r'\bUNITS\s+DISTANCE\s+MICRONS\s+(\d+)\s*;', definition)
    if not dbu_match or int(dbu_match[1]) != geometry['dbu']:
        raise ValueError('routed DEF/geometry DBU mismatch')
    if len(flute_lengths) != len(geometry['nets']):
        raise ValueError('FLUTE net census mismatch')
    size = nx * ny
    demand, capacity = [[0.] * size for _ in range(6)], [0.] * 6
    detour_sum, detour_weight = [0.] * size, [0.] * size
    for index, layer in enumerate(layers):
        if index == 0:
            continue
        group = 0 if index <= 2 else 1 if index <= 4 else 2
        axis = {'HORIZONTAL': 0, 'VERTICAL': 1}.get(layer['direction'])
        if axis is None:
            raise ValueError('invalid routing layer direction')
        pitch = layer['pitch_y' if axis == 0 else 'pitch_x']
        if not math.isfinite(pitch) or pitch <= 0:
            raise ValueError('invalid routing layer pitch')
        capacity[2 * group + axis] += 1 / pitch

    def tile(x, y):
        if not lx <= x <= hx or not ly <= y <= hy:
            return None
        return min(nx - 1, int((x - lx) / dx)) * ny + min(ny - 1, int((y - ly) / dy))

    def lengths(x, y, u, v):
        horizontal = y == v
        fixed, lo, hi = (y, x, u) if horizontal else (x, y, v)
        lower, upper = (ly, hy) if horizontal else (lx, hx)
        if not lower <= fixed <= upper:
            return
        origin, step, bins = (lx, dx, nx) if horizontal else (ly, dy, ny)
        lo, hi = max(lo, origin), min(hi, origin + step * bins)
        if hi <= lo:
            return
        first, last = max(0, int((lo - origin) / step)), min(bins - 1, int((hi - origin) / step))
        for b in range(first, last + 1):
            length = max(0., min(hi, origin + (b + 1) * step) - max(lo, origin + b * step))
            px, py = (origin + (b + .5) * step, fixed) if horizontal else (fixed, origin + (b + .5) * step)
            if length:
                yield tile(px, py), length

    census = []
    for net, flute in zip(geometry['nets'], flute_lengths):
        if not net.get('enabled', True) or len(net['pins']) < 2:
            continue
        if net['name'] not in routes:
            raise ValueError(f'missing routed net: {net["name"]}')
        segments = routes[net['name']]
        length = math.fsum(abs(u - x) + abs(v - y) for _, x, y, u, v in segments)
        shape_length = counts['net_shape_lengths'][net['name']]
        if flute is None or not math.isfinite(flute) or flute < 0 or (flute == 0 and shape_length != 0):
            raise ValueError('invalid/degenerate FLUTE denominator')
        if flute > 0 and length == 0:
            raise ValueError(f'no wires for enabled net: {net["name"]}')
        ratio = shape_length / flute if flute else 0.
        census.append({'net': net['name'], 'wirelength_dbu': shape_length, 'centerline_union_dbu': length,
                       'flute_dbu': flute, 'detour': ratio})
        for layer, x, y, u, v in segments:
            index, axis = lookup[layer], int(x == u)
            group = 0 if index <= 2 else 1 if index <= 4 else 2
            channel = 2 * group + axis
            for i, weight in lengths(x, y, u, v):
                if index:
                    demand[channel][i] += weight
                detour_sum[i] += ratio * weight
                detour_weight[i] += weight
    for c in range(6):
        if capacity[c] == 0 and any(demand[c]):
            raise ValueError('wire demand without preferred-direction group capacity')
        demand[c] = [v / (dx * dy * capacity[c]) if capacity[c] else 0. for v in demand[c]]
    drc_map = [0.] * size
    markers = drc_markers(drc, geometry['dbu'])
    outside = 0
    for x, y in markers:
        i = tile(x, y)
        if i is None:
            outside += 1
        else:
            drc_map[i] += 1.
    # DRC counts each marker once. Detour is routed-length weighted per tile.
    detour = [s / w if w else 0. for s, w in zip(detour_sum, detour_weight)]
    grouped = [resample(c, nx, ny, ox, oy) for c in demand]
    utilization = [max(c[i] for c in grouped) for i in range(ox * oy)]
    labels = raw([utilization, resample(drc_map, nx, ny, ox, oy), resample(detour, nx, ny, ox, oy)], ox, oy)
    counts.pop('net_shape_lengths')
    return labels, raw(grouped, ox, oy), dict(counts, drc_markers=len(markers), drc_outside_core=outside,
                                                 nets=census, nominal_group_capacity_per_dbu=capacity,
                                                 route_grid=route_grid, network_grid=[ox, oy])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('geometry', 'def', 'drc', 'flute-lengths', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--bins', type=int, default=64)
    args = parser.parse_args()
    labels, groups, report = extract(json.loads(args.geometry.read_text()), text_file(getattr(args, 'def')),
                                     text_file(args.drc), json.loads(args.flute_lengths.read_text()), args.bins, args.bins)
    args.output.mkdir(parents=True, exist_ok=True)
    write(args.output / 'labels.bin', labels)
    write(args.output / 'layer_groups.bin', groups)
    (args.output / 'extraction.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()
