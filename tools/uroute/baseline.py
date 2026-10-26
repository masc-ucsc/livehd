# SPDX-License-Identifier: BSD-3-Clause
"""Score native bbox/RSMT RUDY against one routed pool sample's wire labels."""
import argparse
import json
from pathlib import Path
import subprocess

from .features import resample
from .maps import read
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


def baseline(sample, output, binary):
    sample, output = Path(sample), Path(output)
    metadata = json.loads((sample / 'sample.json').read_text())
    if metadata['status'] != 'routed':
        raise ValueError('baseline reference must be routed')
    nx, ny = metadata['route_grid']
    if nx != ny:
        raise ValueError('rudy_metrics currently requires a square source grid')
    geometry = json.loads((sample / 'geometry.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    physical = output / 'physical.txt'
    physical.write_text(snapshot(geometry, nx))
    reference = read(sample / 'labels.bin')
    report = {'sample_id': metadata['id'], 'source_geometry': metadata['routed_def_sha256'],
              'reference': 'max of directional M2-M3, M4-M5, M6+ nominal-track utilization; M1 access excluded',
              'baseline_capacity': 'native preferred-direction aggregate over all LEF routing layers, including M1',
              'net_scope': 'same enabled data nets as features and routed wires (clock/reset excluded)', 'models': {}}
    for model in ('rudy', 'rsmt'):
        prefix = output / model
        subprocess.run([str(Path(binary).resolve()), str(physical.resolve()), str(prefix.resolve()), model], check=True, timeout=120)
        variants = {}
        for mode in ('base', 'enhanced'):
            _, _, values = read_rudy_tsv(output / f'{model}_{mode}.tsv', (nx, ny))
            values = resample(values, nx, ny, reference.bins_x, reference.bins_y)
            variants[mode] = metrics(absolute_channel(reference, 0), values, reference.bins_x, reference.bins_y)
        report['models'][model] = variants
    (output / 'baseline.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rudy-metrics', type=Path, default=Path('bazel-bin/tools/rudy_metrics'))
    args = parser.parse_args()
    print(json.dumps(baseline(args.sample, args.output, args.rudy_metrics), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
