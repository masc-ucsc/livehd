#!/usr/bin/env python3
"""Export measured macro/density frames from magnet.json as standalone PNGs.

Uses matplotlib for engineering plots. No cells, inferred poses or layout edits.
The paired export selects the GP frame before the first macro legalization and
the recorded snap result, rather than comparing two later fixed-macro frames.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR', '/tmp/livehd-magnet-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.patches import Rectangle
import numpy as np


def plot(report, index, destination):
    frame = report['frames'][index]
    unit = report['dbu_per_um']
    box = np.asarray(report['layout'], dtype=float) / unit
    density = np.asarray(frame['density'], dtype=float)
    if density.size != frame['bins_x'] * frame['bins_y'] or not np.isfinite(density).all():
        raise ValueError('malformed regional density frame')
    density = density.reshape(frame['bins_x'], frame['bins_y']).T
    figure, axis = plt.subplots(figsize=(10, 8), constrained_layout=True)
    colors = LinearSegmentedColormap.from_list('cell_density', ['#142236', '#24969c', '#f3d35d', '#ff4536'])
    image = axis.imshow(density, origin='lower', extent=[box[0], box[2], box[1], box[3]],
                        vmin=0, vmax=2 * report['target_density'], cmap=colors, interpolation='nearest')
    groups = {}
    for macro in frame['macros']:
        x, y = macro['x'] / unit, macro['y'] / unit
        width, height = macro['width'] / unit, macro['height'] / unit
        axis.add_patch(Rectangle((x, y), width, height, linewidth=.4, edgecolor='#061019',
                                 facecolor='#96a4b7' if macro['fixed'] else '#94caff', alpha=.85))
        group = macro['group']
        if group:
            bounds = groups.setdefault(group, [x, y, x + width, y + height])
            bounds[:] = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x + width), max(bounds[3], y + height)]
    for group, (xl, yl, xh, yh) in groups.items():
        axis.add_patch(Rectangle((xl, yl), xh - xl, yh - yl, fill=False, linewidth=1.1,
                                 edgecolor='white', linestyle='--'))
        if len(groups) <= 64:
            axis.annotate(f'G{group}', (xl, yh), color='white', fontsize=7, xytext=(1, 2), textcoords='offset points')
    axis.set(xlim=(box[0], box[2]), ylim=(box[1], box[3]), aspect='equal', xlabel='x (µm)', ylabel='y (µm)',
             title=f"Iteration {frame['iteration']} · {frame['event']} · {len(groups)} rigid groups")
    figure.colorbar(image, ax=axis, label='Movable standard-cell footprint density')
    figure.savefig(destination, dpi=150)
    plt.close(figure)
    return {'frame': index, 'iteration': frame['iteration'], 'event': frame['event'], 'groups': len(groups)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frame', type=int, help='export one frame instead of the pre/post pair')
    args = parser.parse_args()
    data = args.report.read_bytes()
    report = json.loads(data)
    if not report['frames'] or report['dbu_per_um'] <= 0:
        parser.error('report has no measured frames or invalid units')
    args.output.mkdir(parents=True, exist_ok=True)
    if args.frame is not None:
        if not 0 <= args.frame < len(report['frames']):
            parser.error('frame index outside measured history')
        selected = {'frame.png': args.frame}
    else:
        snap = next((i for i, f in enumerate(report['frames']) if f['event'] in ('snap', 'snap_disabled')), None)
        if snap is None:
            parser.error('report has no measured snap decision')
        first_legal = next((i for i, f in enumerate(report['frames'][:snap + 1]) if f['event'] == 'legalization'), snap)
        before = next((i for i in range(first_legal - 1, -1, -1)
                       if report['frames'][i]['event'] in ('global placement', 'global_placement')), None)
        if before is None:
            parser.error('report has no GP frame before macro legalization')
        selected = {'before_legalization.png': before, 'after_legalization.png': snap}
    result = {'input': str(args.report.resolve()), 'input_sha256': hashlib.sha256(data).hexdigest(),
              'renderer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'matplotlib_version': matplotlib.__version__,
              'scope': 'Measured macro bodies and regional cell density; no individual cells', 'images': {}}
    for name, index in selected.items():
        result['images'][name] = plot(report, index, args.output / name)
    (args.output / 'snapshots.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
