# SPDX-License-Identifier: BSD-3-Clause
"""Score native bbox RUDY and RSMT RUDY against the routed wire labels of one layout.

python -m tools.routetruth.baseline --geometry geometry.json --labels labels/labels.bin \
  --output scores/ [--bins 64] [--rudy-metrics bazel-bin/tools/rudy_metrics]

geometry.json and labels.bin must come from the same routed DEF. --bins is the
grid the estimators run on (the placer's route grid); their maps are then
area-resampled to the label grid before scoring.
"""
import argparse
import json
from pathlib import Path
import subprocess

from .maps import read, resample
from .score import absolute_channel, metrics, read_rudy_tsv


def snapshot(geometry, bins):
    lx, ly, hx, hy = geometry['core']
    header = [lx, ly, hx, hy, geometry['site_width'], geometry['row_height'],
              geometry['capacity_h'], geometry['capacity_v'], bins]
    nodes = geometry['nodes']
    lines = [' '.join(map(str, header)), str(len(nodes))]
    for n in nodes:
        lines.append(' '.join(map(str, [n['x'], n['y'], 0 if n.get('mask') else n['width'],
                                      0 if n.get('mask') else n['height'], int(n.get('macro', False) and not n.get('mask')),
                                      0 if n.get('mask') else n.get('blocked_h', 0),
                                      0 if n.get('mask') else n.get('blocked_v', 0)])))
    nets = [n for n in geometry['nets'] if n.get('enabled', True) and len(n['pins']) >= 2]
    lines.append(str(len(nets)))
    for n in nets:
        if len(n['pin_nodes']) != len(n['pins']):
            raise ValueError('physical pin-node census mismatch')
        pins = [value for node, (x, y) in zip(n['pin_nodes'], n['pins']) for value in (node, x, y)]
        lines.append(' '.join(map(str, [len(n['pins']), *pins])))
    return '\n'.join(lines) + '\n'


def baseline(geometry, labels, output, binary, bins=64):
    """geometry: the route-geometry document; labels: the label Maps of the same routed DEF."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    physical = output / 'physical.txt'
    physical.write_text(snapshot(geometry, bins))
    report = {'top': geometry.get('top'), 'route_grid': [bins, bins], 'label_grid': [labels.bins_x, labels.bins_y],
              'reference': 'max of directional M2-M3, M4-M5, M6+ nominal-track utilization; M1 access excluded',
              'baseline_capacity': 'native preferred-direction aggregate over all LEF routing layers, including M1',
              'net_scope': 'same enabled data nets as the routed wire labels (clock/reset excluded)', 'models': {}}
    for model in ('rudy', 'rsmt'):
        prefix = output / model
        subprocess.run([str(Path(binary).resolve()), str(physical.resolve()), str(prefix.resolve()), model], check=True, timeout=120)
        variants = {}
        for mode in ('base', 'enhanced'):
            _, _, values = read_rudy_tsv(output / f'{model}_{mode}.tsv', (bins, bins))
            values = resample(values, bins, bins, labels.bins_x, labels.bins_y)
            variants[mode] = metrics(absolute_channel(labels, 0), values, labels.bins_x, labels.bins_y)
        report['models'][model] = variants
    (output / 'baseline.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--geometry', type=Path, required=True)
    parser.add_argument('--labels', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bins', type=int, default=64, help='estimator grid per axis (rudy_metrics uses a square grid)')
    parser.add_argument('--rudy-metrics', type=Path, default=Path('bazel-bin/tools/rudy_metrics'))
    args = parser.parse_args()
    report = baseline(json.loads(args.geometry.read_text()), read(args.labels), args.output, args.rudy_metrics, args.bins)
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
