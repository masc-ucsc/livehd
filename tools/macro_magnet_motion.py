#!/usr/bin/env python3
"""Plot macro stability from measured SRAM movement in a native magnet.json.

Uses every valid GP step, independently of the animation's frame sampling.
Exports PNG/SVG, CSV, a summary and a standalone HTML with an embedded CSV.
Packing and snap/LP movement are measured from recorded poses separately.
"""
import argparse
import base64
import csv
import gzip
import hashlib
import html
import io
import json
import math
import os
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR', '/tmp/livehd-magnet-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def analyze(report):
    unit = report['dbu_per_um']
    if not math.isfinite(unit) or unit <= 0:
        raise ValueError('invalid DBU per micron')
    steps = [s for s in report['motion_history'] if s['center_step_valid']]
    if not steps:
        raise ValueError('recording has no exact GP center-step measurements')
    iterations = [s['iteration'] for s in steps]
    if any(a >= b for a, b in zip(iterations, iterations[1:])):
        raise ValueError('valid GP steps must be in increasing iteration order')
    counts = {s['physical_macro_count'] for s in steps}
    if len(counts) != 1 or next(iter(counts)) <= 0:
        raise ValueError('physical SRAM population changed during GP')
    absolute = [s['center_step_sum_dbu'] / unit for s in steps]
    if any(not math.isfinite(v) or v < 0 for v in absolute):
        raise ValueError('invalid absolute center-step distance')
    baseline = [s for s in steps if 1 <= s['iteration'] <= 50]
    if not baseline:
        raise ValueError('recording has no valid step in iterations 1–50')
    peak = max(baseline, key=lambda s: s['center_step_sum_dbu'])
    reference = peak['center_step_sum_dbu'] / unit
    # A truly motionless initial window has no nonzero reference. Do not turn
    # later movement into zero percent or divide by an arbitrary small number.
    relative = [100 * v / reference for v in absolute] if reference else None
    actions = []
    frames = report['frames']
    for i, frame in enumerate(frames):
        if i == 0 or frame['event'] not in ('magnet', 'snap', 'snap_disabled'):
            continue
        j = i - 1
        if frame['event'] == 'magnet':
            if frames[j]['event'] != 'before_magnet':
                raise ValueError('merge has no immediately preceding pose')
        else:
            # The ordinary observer emits the legalized pose before the snap
            # marker. Skip that duplicate when measuring the snap/LP jump.
            while j >= 0 and frames[j]['event'] == 'legalization':
                j -= 1
        if j < 0:
            raise ValueError('snap has no pre-legalization pose')
        before = {m['name']: m for m in frames[j]['macros']}
        after = {m['name']: m for m in frame['macros']}
        if before.keys() != after.keys():
            raise ValueError('macro identities changed across recorded action')
        distance = sum(math.hypot(
            before[n]['x'] + before[n]['width'] / 2 - after[n]['x'] - after[n]['width'] / 2,
            before[n]['y'] + before[n]['height'] / 2 - after[n]['y'] - after[n]['height'] / 2)
            for n in before) / unit
        actions.append({'iteration': frame['iteration'], 'event': frame['event'],
                        'from_frame': j, 'to_frame': i,
                        'sum_center_displacement_um': distance})
    summary = {
        'metric': 'macro_stability',
        'gp_steps': len(steps), 'physical_srams': next(iter(counts)),
        'reference_steps': len(baseline), 'reference_peak_um': reference,
        'reference_peak_iteration': peak['iteration'],
        'last50_mean_percent': sum(relative[-50:]) / len(relative[-50:]) if relative else None,
        'last50_peak_percent': max(relative[-50:]) if relative else None,
        'outside_gp_actions': actions,
        'scope': 'Sum of consecutive physical SRAM center distances per valid GP step. '
                 'Meta-macro members count separately. Merge and snap/LP jumps are separate. '
                 'Normalized to the maximum of valid GP steps in iterations 1–50; no smoothing. '
                 'This diagnostic does not define a magnetization gate or prove convergence.'}
    return steps, absolute, relative, summary


def render(report_path, output, title):
    raw = report_path.read_bytes()
    report = json.loads(raw)
    steps, absolute, relative, summary = analyze(report)
    summary.update(source_sha256=hashlib.sha256(raw).hexdigest(),
                   renderer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    output.mkdir(parents=True, exist_ok=True)
    warp_trace = report.get('warp_stability', [])
    if warp_trace:
        summary['scope'] += ' Additional markers show the recorded window means; the per-step curves remain unsmoothed.'
        if warp_trace[0].get('policy') == 'constant':
            summary['scope'] += ' Stability controls activation only; strength and subsequent refresh cadence are constant.'
    fig, axes = plt.subplots(3 if warp_trace else 2, 1, figsize=(12, 9.0 if warp_trace else 7.3), sharex=True)
    labels = ('Total SRAM center movement\n(µm / GP step)', 'Macro stability\n(% of first-50 peak)')
    iterations = [s['iteration'] for s in steps]
    for ax, values, label in zip(axes, (absolute, relative), labels):
        if values is not None:
            ax.plot(iterations, values, color='#1253a4', lw=.8, label='Sum of physical SRAM center-step distances')
        else:
            ax.text(.5, .5, 'No nonzero first-50 reference', transform=ax.transAxes, ha='center')
        ax.axvspan(0, 50, color='#73b5d6', alpha=.15, label='First 50 GP iterations (reference)')
        ax.set_ylabel(label)
        ax.grid(alpha=.2)
        ax.set_ylim(bottom=0)
        seen = set()
        for event in report['events']:
            style = {'pull': ('#7a3bb2', 'Warpspace anchors'), 'timing': ('#777777', 'Timing update'),
                     'magnet': ('#e18a16', 'Magnet merge'), 'snap': ('#111111', 'Macro snap/LP')}.get(event['event'])
            if style:
                ax.axvline(event['iteration'], color=style[0], lw=.7, alpha=.45,
                           label=style[1] if event['event'] not in seen else None)
                seen.add(event['event'])
    if warp_trace:
        checks = [s for s in warp_trace if s['macro_stability_fraction'] is not None]
        axes[1].plot([s['iteration'] for s in checks], [100*s['macro_stability_fraction'] for s in checks],
                     color='#c47713', marker='o', ms=3, lw=.8, ls='--',
                     label=f"Recorded {warp_trace[0]['window']}-step mean at refresh")
        axes[2].step([s['iteration'] for s in warp_trace] + [max(iterations)],
                     [s['weight'] for s in warp_trace] + [warp_trace[-1]['weight']],
                     where='post', color='#7a3bb2', label='Warpspace strength')
        axes[2].set_ylabel('Warpspace strength')
        axes[2].set_ylim(0, 1.05)
        axes[2].grid(alpha=.2)
        rollbacks = [s for s in warp_trace if s['decision'] == 'rollback']
        if rollbacks:
            axes[2].scatter([s['iteration'] for s in rollbacks], [s['weight'] for s in rollbacks], color='#b52125', marker='D', s=30, zorder=4, label='Rollback: motion worsened')
        trials = [s for s in warp_trace if s.get('trial_reference_fraction') is not None and s['decision'] in ('increase', 'rollback', 'hold_maximum')]
        if trials:
            axes[1].plot([s['iteration'] for s in trials], [100*s['trial_reference_fraction'] for s in trials], color='#777777', marker='x', lw=.7, ls=':', label='Pre-increase motion reference')
        if warp_trace[0].get('target', 0) > 0:
            axes[1].axhline(100 * warp_trace[0]['target'], color='#25834e', ls=':', lw=1, label='Motion reference (ramp)' if warp_trace[0].get('ramp_up') else 'Adaptive stability target')
        start_target = warp_trace[0].get('start_stability_target', 0)
        if start_target > 0:
            axes[1].axhline(100 * start_target, color='#ab681b', ls='--', lw=1, label='Warpspace start threshold')
    if relative is not None:
        axes[1].axhline(100, color='#b52125', ls='--', lw=1, label='First-50 peak = 100%')
    axes[-1].set_xlabel('Macro GP iteration')
    axes[1].set_xlim(0, max(iterations))
    axes[0].set_title(title)
    handles, labels = axes[0].get_legend_handles_labels()
    if relative is not None:
        h, l = axes[1].get_legend_handles_labels()
        for handle, label in zip(h, l):
            if label not in labels:
                handles.append(handle)
                labels.append(label)
    if warp_trace:
        h, l = axes[2].get_legend_handles_labels()
        handles.extend(h)
        labels.extend(l)
    fig.legend(handles, labels, loc='lower center', ncol=3, fontsize=8, bbox_to_anchor=(.5, .01))
    fig.tight_layout(rect=(0, .1, 1, 1))
    fig.savefig(output / 'center_step.svg')
    fig.savefig(output / 'center_step.png', dpi=110)
    plt.close(fig)
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(('gp_iteration', 'total_center_step_um', 'percent_of_first50_peak', 'physical_sram_count'))
    for i, (step, absolute_value) in enumerate(zip(steps, absolute)):
        writer.writerow((step['iteration'], absolute_value, relative[i] if relative is not None else '',
                         step['physical_macro_count']))
    (output / 'center_step.csv').write_text(buf.getvalue())
    payload = base64.b64encode(gzip.compress(buf.getvalue().encode(), mtime=0)).decode()
    svg = (output / 'center_step.svg').read_text()
    svg = svg[svg.index('<svg'):]
    rows = ''.join('<tr><td>{}</td><td>{}</td><td>{:.3f}</td></tr>'.format(
        a['iteration'], 'macro snap + legalization' if a['event'] == 'snap' else
        'macro legalization (snap disabled)' if a['event'] == 'snap_disabled' else 'magnet merge',
        a['sum_center_displacement_um']) for a in summary['outside_gp_actions'])
    body = f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(title)}</title>
<style>body{{font:16px system-ui;color:#182330;margin:20px;max-width:1500px}}svg{{width:100%;height:auto}}p{{line-height:1.5}}td,th{{padding:5px 15px;text-align:left}}button{{font:inherit;padding:8px}}</style>
<h1>{html.escape(title)}</h1><p>{len(steps)} measured GP steps; {summary['physical_srams']} physical SRAMs throughout. Meta-macro members count separately.</p>{svg}
<p>{html.escape(summary['scope'])}</p><p>Absolute movement is <code>sum_i distance(center_i[t],center_i[t-1])</code>, in µm per GP step. Opposite directions cannot cancel.
The first-50 reference is {summary['reference_peak_um']:.3f} µm at iteration {summary['reference_peak_iteration']}. Invalid step boundaries are omitted from GP measurements, never filled with zeros.</p>
<h2>Movement outside GP</h2><table><tr><th>Iteration</th><th>Action</th><th>Total center displacement (µm)</th></tr>{rows}</table>
<button id="csv">Download all GP step measurements (CSV)</button><p>Source SHA-256: <code>{summary['source_sha256']}</code></p>
<script>document.getElementById('csv').onclick=async()=>{{const bytes=Uint8Array.from(atob('{payload}'),c=>c.charCodeAt(0));const data=await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).blob();const a=document.createElement('a');a.href=URL.createObjectURL(data);a.download='center-step.csv';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);}};</script></html>'''
    (output / 'center_step.html').write_text(body)
    summary['html_bytes'] = len(body.encode())
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--title', default='Macro stability: SRAM center movement per GP step')
    args = parser.parse_args()
    print(json.dumps(render(args.report, args.output, args.title)))


if __name__ == '__main__':
    main()
