"""Read-only views of a domplace run (todo/livehd/2d-domplace.html, H): the
written DEF, LEF macro sizes, Liberty cell areas, and the area of a placement
on both bases. Shared by the domplace smokes and scripts/domplace/arms.py;
standard library only."""
from collections import namedtuple
import json
from pathlib import Path
import re

Component = namedtuple('Component', 'macro x y orientation status')
Row = namedtuple('Row', 'name site x y orientation count step')
Def = namedtuple('Def', 'dbu die rows components nets row_list')
DOMINO_PREFIX = '__lhd_domino_'


def leaf(instance):
    """The state name of a flat instance path."""
    return instance.rsplit('.', 1)[-1]


def rail_template(master, orientation):
    """The PG rail template of `master` (<base>_v<k>_t<t>) a start row of this
    orientation needs: t0 on N/FN rows, its t1 twin on FS/S rows."""
    base = master.rpartition('_t')[0]
    return f'{base}_t{1 if orientation in ("FS", "S") else 0}'


def read_def(path):
    """Rows (y -> orientation), components (name -> Component), nets
    (name -> [(instance or PIN, pin)]), the DBU, the die (xl, yl, xh, yh) and
    every ROW in order."""
    dbu, die, rows, components, nets, row_list = None, None, {}, {}, {}, []
    section = None
    for line in Path(path).read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] in ('COMPONENTS', 'NETS', 'PINS'):
            section = words[0]
        elif words[0] == 'END':
            section = None
        elif words[:3] == ['UNITS', 'DISTANCE', 'MICRONS']:
            dbu = int(words[3])
        elif words[0] == 'DIEAREA':
            numbers = [int(w) for w in words if w.lstrip('-').isdigit()]
            die = tuple(numbers[:4])
        elif words[0] == 'ROW':
            rows[int(words[4])] = words[5]
            row_list.append(Row(words[1], words[2], int(words[3]), int(words[4]), words[5], int(words[7]), int(words[11])))
        elif section == 'COMPONENTS' and words[0] == '-' and len(words) > 9:
            components[words[1]] = Component(words[2], int(words[6]), int(words[7]), words[9], words[4])
        elif section == 'NETS' and words[0] == '-':
            nets[words[1]] = [(words[n + 1], words[n + 2]) for n, word in enumerate(words) if word == '(']
    return Def(dbu, die, rows, components, nets, row_list)


def lef_sizes(paths):
    """Macro (width, height) in microns over the given LEF files."""
    sizes, macro = {}, None
    for path in paths:
        for line in Path(path).read_text().splitlines():
            words = line.split()
            if words[:1] == ['MACRO']:
                macro = words[1]
            elif words[:1] == ['SIZE'] and macro:
                sizes[macro] = (float(words[1]), float(words[3]))
    return sizes


def lef_sites(paths):
    """Site (width, height) in microns over the given LEF files."""
    sites, site = {}, None
    for path in paths:
        for line in Path(path).read_text().splitlines():
            words = line.split()
            if words[:1] == ['SITE'] and len(words) == 2:
                site = words[1]
            elif words[:1] == ['SIZE'] and site:
                sites[site] = (float(words[1]), float(words[3]))
            elif words[:1] == ['END']:
                site = None
    return sites


def liberty_areas(paths):
    """Cell areas over the given Liberty files (the first area of each cell)."""
    areas = {}
    for path in paths:
        name = None
        for line in Path(path).read_text().splitlines():
            cell = re.match(r'\s*cell\s*\(\s*"?([^")]+)"?\s*\)', line)
            if cell:
                name = cell.group(1)
                continue
            area = re.match(r'\s*area\s*:\s*([-0-9.eE+]+)', line)
            if area and name and name not in areas:
                areas[name] = float(area.group(1))
    return areas


def area_report(view, sizes, liberty, sidecar=None):
    """Placed area on both bases. The LEF basis is every component's footprint;
    the Liberty basis charges CMOS cells their Liberty area and each domino
    variant its sidecar Liberty-basis estimate. Rail inverters are ordinary
    cells of the netlist, so they are counted once, with the CMOS cells."""
    variants = {}
    if sidecar:
        for master in json.loads(Path(sidecar).read_text())['masters']:
            for v in master['variants']:
                variants[v['name']] = v
    out = {'lef_um2': 0.0, 'domino_lef_um2': 0.0, 'liberty_um2': 0.0, 'domino_instances': 0, 'cells': 0,
           'variants': {}, 'unknown': []}
    for component in view.components.values():
        w, h = sizes.get(component.macro, (0.0, 0.0))
        out['lef_um2'] += w * h
        out['cells'] += 1
        if component.macro.startswith(DOMINO_PREFIX):
            out['domino_instances'] += 1
            out['domino_lef_um2'] += w * h
            out['variants'][component.macro] = out['variants'].get(component.macro, 0) + 1
            estimate = variants.get(component.macro, {}).get('a_estimate_um2')
            if estimate is None:
                out['unknown'].append(component.macro)
            else:
                out['liberty_um2'] += estimate
        elif component.macro in liberty:
            out['liberty_um2'] += liberty[component.macro]
        else:
            out['unknown'].append(component.macro)
    out['unknown'] = sorted(set(out['unknown']))
    return out
