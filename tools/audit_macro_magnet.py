#!/usr/bin/env python3
"""Independent DEF/LEF macro halo and rigid-member handoff audit.

OpenROAD check_placement supplies row/site legality separately. This checks
physical boxes, rounded native halos, and flat group offsets without importing
the placer or relying on its legality result.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np


def components(text):
    section = re.search(r'^COMPONENTS\s+\d+\s*;(.*?)^END COMPONENTS', text, re.M | re.S)
    if not section:
        raise ValueError('missing DEF components')
    result = {}
    for entry in section[1].split(';'):
        match = re.search(r'-\s+(\S+)\s+(\S+).*?\+\s+(PLACED|FIXED|COVER)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\S+)', entry, re.S)
        if match:
            name, master, status, x, y, orientation = match.groups()
            result[name] = {'master': master, 'status': status, 'x': int(x), 'y': int(y), 'orientation': orientation}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--def', dest='definition', type=Path, required=True)
    parser.add_argument('--lef', type=Path, action='append', required=True)
    parser.add_argument('--qor', type=Path, required=True)
    parser.add_argument('--magnet', type=Path)
    parser.add_argument('--fixed-frame', type=Path)
    parser.add_argument('--macro-only', action='store_true',
                        help='audit macro clearance, core bounds and rigid offsets before cell legalization; no fixed-handoff claim')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    text = args.definition.read_text()
    units = int(re.search(r'UNITS DISTANCE MICRONS\s+(\d+)', text)[1])
    masters, sites = {}, {}
    for path in args.lef:
        lef = path.read_text()
        for site in re.finditer(r'^SITE\s+(\S+)(.*?)^END\s+\1(?:\s|$)', lef, re.M | re.S):
            size = re.search(r'\bSIZE\s+([\d.eE+-]+)\s+BY\s+([\d.eE+-]+)', site[2])
            if size:
                sites[site[1]] = (round(float(size[1]) * units), round(float(size[2]) * units))
        for macro in re.finditer(r'\bMACRO\s+(\S+)(.*?)\bEND\s+\1(?:\s|$)', lef, re.S):
            size = re.search(r'\bSIZE\s+([\d.eE+-]+)\s+BY\s+([\d.eE+-]+)', macro[2])
            klass = re.search(r'\bCLASS\s+(\w+)', macro[2])
            if size and klass:
                masters[macro[1]] = (round(float(size[1]) * units), round(float(size[2]) * units), klass[1] != 'CORE')
    cells = components(text)
    names = list(cells)
    boxes, macros = [], []
    for name in names:
        cell = cells[name]
        width, height, macro = masters[cell['master']]
        if cell['orientation'] in ('E', 'W', 'FE', 'FW'):
            width, height = height, width
        boxes.append([cell['x'], cell['y'], cell['x'] + width, cell['y'] + height])
        macros.append(macro)
    boxes = np.asarray(boxes, dtype=np.int64)
    macro_mask = np.asarray(macros, dtype=bool)
    qor = json.loads(args.qor.read_text())
    halo = qor.get('macro_halo', qor['halo'])
    hx, hy = halo['x_dbu'], halo['y_dbu']
    cell_hx, cell_hy = qor['halo']['x_dbu'], qor['halo']['y_dbu']
    macro_indexes = np.flatnonzero(macro_mask)
    cell_indexes = np.flatnonzero(~macro_mask)
    row_boxes = []
    for row in re.finditer(r'^ROW\s+\S+\s+(\S+)\s+(-?\d+)\s+(-?\d+)\s+\S+\s+DO\s+(\d+)\s+BY\s+(\d+)\s+STEP\s+(\d+)\s+(\d+)\s*;', text, re.M):
        site, x, y, nx, ny, sx, sy = row.groups()
        x, y, nx, ny, sx, sy = map(int, (x, y, nx, ny, sx, sy))
        sw, sh = sites[site]
        row_boxes.append([x, y, x + (nx - 1) * sx + sw, y + (ny - 1) * sy + sh])
    if not row_boxes:
        raise ValueError('missing core rows for macro bounds audit')
    core = [min(b[0] for b in row_boxes), min(b[1] for b in row_boxes),
            max(b[2] for b in row_boxes), max(b[3] for b in row_boxes)]
    bounds_errors = [names[i] for i in macro_indexes
                     if boxes[i, 0] < core[0] or boxes[i, 1] < core[1]
                     or boxes[i, 2] > core[2] or boxes[i, 3] > core[3]]
    padded_macros = boxes[macro_mask] + np.asarray([-hx, -hy, hx, hy])
    ordinary = boxes[~macro_mask]
    violations = []
    for index in macro_indexes:
        a = boxes[index].copy() + np.asarray([-hx, -hy, hx, hy])
        hits = np.flatnonzero((a[2] > padded_macros[:, 0]) & (a[0] < padded_macros[:, 2]) &
                              (a[3] > padded_macros[:, 1]) & (a[1] < padded_macros[:, 3]))
        for other in macro_indexes[hits]:
            if other > index:
                violations.append([names[index], names[other]])
        if not args.macro_only:
            a = boxes[index].copy() + np.asarray([-cell_hx, -cell_hy, cell_hx, cell_hy])
            hits = np.flatnonzero((a[2] > ordinary[:, 0]) & (a[0] < ordinary[:, 2]) &
                                  (a[3] > ordinary[:, 1]) & (a[1] < ordinary[:, 3]))
            violations.extend([names[index], names[other]] for other in cell_indexes[hits])
    handoff_errors = []
    if args.fixed_frame:
        for name, before in components(args.fixed_frame.read_text()).items():
            if before['status'] == 'FIXED' and (name not in cells or before != cells[name]):
                handoff_errors.append(name)
    group_errors = []
    groups = []
    if args.magnet:
        groups = json.loads(args.magnet.read_text())['groups']
        for group in groups:
            origins = []
            for member in group['members']:
                cell = cells[member['name']]
                origins.append((cell['x'] - member['x_offset'], cell['y'] - member['y_offset']))
                if cell['status'] != 'FIXED' and not args.macro_only:
                    group_errors.append({'group': group['id'], 'member': member['name'], 'reason': 'not fixed'})
            if origins and any(abs(x - origins[0][0]) > 1e-5 or abs(y - origins[0][1]) > 1e-5 for x, y in origins):
                group_errors.append({'group': group['id'], 'reason': 'member offsets changed'})
    result = {'status': 'pass' if not (violations or handoff_errors or group_errors or bounds_errors) else 'failed',
              'scope': 'macro-only; cells not audited, no fixed-handoff claim' if args.macro_only else 'macro/cell clearance and rigid fixed handoff',
              'macro_count': int(macro_mask.sum()), 'cells': len(cells), 'groups': len(groups),
              'core_bounds_dbu': core, 'macro_bounds_errors': bounds_errors,
              'rounded_halo_dbu': [hx, hy], 'halo_violations': violations,
              'rounded_cell_halo_dbu': [cell_hx, cell_hy],
              'fixed_frame_changes': handoff_errors, 'rigid_group_errors': group_errors,
              'def_sha256': hashlib.sha256(args.definition.read_bytes()).hexdigest(),
              'auditor_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('halo_violations', 'fixed_frame_changes', 'rigid_group_errors', 'macro_bounds_errors')}))
    return int(result['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())
