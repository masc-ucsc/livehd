#!/usr/bin/env python3
"""F1 experiment on a legal, single-height ASAP7 PCRS handoff with firm protected cells."""

import argparse
import json
import re
import resource
import subprocess
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import NamedTuple


class Pin(NamedTuple):
    net: str
    tracks: dict
    count: int


class Cost(NamedTuple):
    inaccessible: int
    incompatible: int
    scarcity: float


POSE = re.compile(r'\+\s+(PLACED|FIXED|COVER)\s+\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\w+)')
SECTION = re.compile(r'^(COMPONENTS (\d+) ;\n)(.*?)^END COMPONENTS', re.S | re.M)
RECORD = re.compile(r'^[ \t]*-\s+(\S+)\s+(\S+)\s+.*?;', re.S | re.M)
ROW = re.compile(r'^ROW \S+ \S+ (-?\d+) (-?\d+) \S+ DO (\d+) BY 1 STEP (\d+) 0 ;', re.M)


def read_options(path):
    cells = defaultdict(list)
    with path.open() as source:
        for line in source:
            cell, _, net, exhausted, *points = line.rstrip('\n').split('\t')
            if int(exhausted):
                raise ValueError(f'Incomplete pin-access search for {cell} in {path}')
            tracks = defaultdict(set)
            for point in points:
                _, y, left, right = map(float, point.split(','))
                tracks[y].add((left, right))
            tracks = {y: sorted(intervals) for y, intervals in tracks.items()}
            cells[cell].append(Pin(net, tracks, sum(map(len, tracks.values()))))
    return cells


def neighbor_cost(left, right, spacing):
    hard = 0
    risk = 0.0
    for a in left:
        for b in right:
            choices = a.count * b.count
            if a.net == b.net or not choices:
                continue
            conflicts = sum(
                r1 + spacing > l2 and r2 + spacing > l1
                for y in a.tracks.keys() & b.tracks.keys()
                for l1, r1 in a.tracks[y]
                for l2, r2 in b.tracks[y]
            )
            hard += conflicts == choices
            risk += conflicts / choices
    return hard, risk


def local_cost(options, cell, adjacent, spacing):
    pins = options[cell]
    left, right = adjacent
    hard_left, risk_left = neighbor_cost(options[left], pins, spacing)
    hard_right, risk_right = neighbor_cost(pins, options[right], spacing)
    return Cost(sum(p.count == 0 for p in pins), hard_left + hard_right,
                sum(1 / max(1, p.count) for p in pins) + risk_left + risk_right)


def cpu_seconds():
    return sum(r.ru_utime + r.ru_stime for r in
               (resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)))


def run(args):
    start_cpu, start_wall = cpu_seconds(), time.monotonic()
    src = args.layout.read_text()
    blockages = re.search(r'^BLOCKAGES\s+\d+\s*;(.*?)^END BLOCKAGES', src, re.M | re.S)
    if blockages and re.search(r'^\s*-\s+PLACEMENT\b', blockages[1], re.M):
        raise ValueError('Placement blockages need a segmented-row model')
    section = SECTION.search(src)
    if not section:
        raise ValueError('Expected an OpenROAD or LiveHD COMPONENTS section')
    dbu = int(re.search(r'UNITS DISTANCE MICRONS (\d+)', src)[1])
    masters = {}
    tip = None
    for lef in args.lef:
        text = lef.read_text()
        for name, width, height in re.findall(r'^MACRO\s+(\S+).*?^\s*SIZE\s+([.\d]+)\s+BY\s+([.\d]+)', text, re.S | re.M):
            masters[name] = round(float(width) * dbu), round(float(height) * dbu)
        metal = re.search(r'^LAYER M2\s*\n(.*?)^END M2$', text, re.S | re.M)
        if metal:
            rule = re.search(r'EOLKEEPOUT\s+[.\d]+\s+EXTENSION\s+[.\d]+\s+[.\d]+\s+([.\d]+)\s+CORNERONLY', metal[1])
            if rule:
                tip = round(float(rule[1]) * dbu)
    if not tip:
        raise ValueError('Missing ASAP7 M2 end-of-line extension')
    bands = defaultdict(list)
    for x, y, count, step in ROW.findall(src):
        x, y, count, step = map(int, (x, y, count, step))
        bands[y].append((x, x + count * step, step))
    row_y = sorted(bands)
    heights = {b - a for a, b in zip(row_y, row_y[1:])}
    if len(row_y) == 1:
        heights = {masters[r[2]][1] for r in RECORD.finditer(section[3])}
    if len(heights) != 1:
        raise ValueError('Expected equally spaced single-height rows')
    height = heights.pop()
    xs, widths, tokens, fixed = {}, {}, {}, set()
    segments = defaultdict(list)
    for rec in RECORD.finditer(section[3]):
        name, master = rec[1], rec[2]
        pose = POSE.search(rec[0])
        if not pose or pose[4] not in ('N', 'FN', 'FS', 'S'):
            raise ValueError(f'Unsupported pose for {name}')
        x, y = int(pose[2]), int(pose[3])
        width, master_height = masters[master]
        if master_height != height:
            raise ValueError(f'{master} spans a different row height')
        band = next((b for b in bands[y] if b[0] <= x and x + width <= b[1] and (x-b[0]) % b[2] == 0), None)
        if band is None:
            raise ValueError(f'{name} has no legal site in its row segment')
        segments[y, band].append(name)
        if pose[1] != 'PLACED':
            fixed.add(name)
        xs[name], widths[name] = x, width
        offset = section.start(3) + rec.start()
        tokens[name] = offset + pose.start(2), offset + pose.end(2)
    if len(xs) != int(section[2]):
        raise ValueError('Could not account for all components')
    neighbors = {}
    batches = defaultdict(dict)
    for (_, (_, _, step)), cells in segments.items():
        cells.sort(key=xs.get)
        for left, right in zip(cells, cells[1:]):
            if xs[left] + widths[left] > xs[right]:
                raise ValueError(f'Input cells overlap at {right}')
        for i in range(1, len(cells) - 1):
            left, cell, right = cells[i-1:i+2]
            if cell in fixed:
                continue
            neighbors[cell] = left, right
            for delta in (-4, -2, -1, 1, 2, 4):
                site = xs[cell] + delta * step
                if xs[left] + widths[left] <= site and site + widths[cell] <= xs[right]:
                    batches[i % 3, delta][cell] = site
    args.out.mkdir(parents=True, exist_ok=True)

    def apply(path, moves):
        pieces = []
        offset = 0
        for cell in sorted(moves, key=tokens.get):
            lo, hi = tokens[cell]
            pieces.extend((src[offset:lo], str(moves[cell])))
            offset = hi
        pieces.append(src[offset:])
        path.write_text(''.join(pieces))

    def assess(layout, stem):
        data = args.out / f'{stem}.tsv'
        with (args.out / f'{stem}.log').open('w') as log:
            subprocess.run([args.oracle, layout, data, *args.lef], check=True, stdout=log, stderr=log)
        return read_options(data)

    initial = assess(args.layout, 'baseline')
    before = {cell: local_cost(initial, cell, adj, tip) for cell, adj in neighbors.items()}

    def trial(item):
        (phase, delta), moves = item
        stem = f'p{phase}_d{delta}'
        layout = args.out / f'{stem}.def'
        apply(layout, moves)
        options = assess(layout, stem)
        candidates = []
        for cell, site in moves.items():
            old = before[cell]
            new = local_cost(options, cell, neighbors[cell], tip)
            change = old.scarcity - new.scarcity
            if new[:2] > old[:2] or (new[:2] == old[:2] and change < 0.1):
                continue
            candidates.append((old.inaccessible - new.inaccessible, old.incompatible - new.incompatible,
                               change, -abs(delta), cell, site))
        return candidates

    with ThreadPoolExecutor(args.jobs) as jobs:
        ranked = [candidate for batch in jobs.map(trial, sorted(batches.items())) for candidate in batch]
    moves = {}
    occupied = set()
    for *_, cell, site in sorted(ranked, reverse=True):
        window = {cell, *neighbors[cell]}
        if not window.intersection(occupied):
            moves[cell] = site
            occupied.update(window)
    final = args.out / 'positions.def'
    apply(final, moves)
    checked = assess(final, 'accepted')
    after = [local_cost(checked, cell, neighbors[cell], tip) for cell in moves]
    result = dict(moves=moves, selected=len(moves), trial_poses=sum(map(len, batches.values())),
                  before=[sum(before[c][i] for c in moves) for i in range(3)],
                  after=[sum(c[i] for c in after) for i in range(3)],
                  search_cpu_s=cpu_seconds()-start_cpu, search_wall_s=time.monotonic()-start_wall)
    (args.out / 'selection.json').write_text(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--def', dest='layout', type=Path, required=True)
    parser.add_argument('--lef', type=Path, action='append', required=True)
    parser.add_argument('--oracle', type=Path, required=True, help='Built //tools/routefirst:pin_options binary')
    parser.add_argument('--out', type=Path, required=True, help='Directory for layouts, pin options and selection.json')
    parser.add_argument('--jobs', type=int, default=3)
    options = parser.parse_args()
    if options.jobs < 1:
        parser.error('--jobs must be positive')
    if options.layout.resolve().parent == options.out.resolve():
        parser.error('Use a separate output directory to keep the input frame intact')
    run(options)
