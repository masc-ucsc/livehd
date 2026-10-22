#!/usr/bin/env python3
"""Run real Pyrope -> ABC -> FP -> macro -> detailed placement -> STA."""
import argparse
import html
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from generate import CASES, topology
import render

ROOT = Path(__file__).resolve().parent


def command(args, log, timeout):
    with log.open('w') as stream:
        stream.write(json.dumps([str(a) for a in args])+'\n')
        stream.flush()
        completed = subprocess.run([str(a) for a in args], stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    if completed.returncode:
        raise RuntimeError(f'command exited {completed.returncode}; see {log}')


def run(case, args, output):
    directory = output / case['name']
    directory.mkdir(parents=True, exist_ok=True)
    for old in ['metrics.json', 'metrics.html', 'final.png', 'fp_plan.png', 'macro_plan.png',
                'before_legalization.png', 'after_legalization.png']:
        (directory/old).unlink(missing_ok=True)
    # These are this runner's generated scratch trees. Fresh directories ensure
    # a missing stage cannot accidentally consume an older run's snapshot.
    for scratch in ('work', 'macro_work', 'macro_lg'):
        if (directory/scratch).exists():
            shutil.rmtree(directory/scratch)
    points, edges = topology(case)
    render.expected(case, points, edges, directory/'expected.png')
    started = time.monotonic()
    binary_hash = render.digest(args.lhd)
    command([args.lhd, 'compile', ROOT/'macros.v', '--top', 'REG_MACRO', '--set', 'compile.slang.blackbox_attribute=true', '--set', 'lhd.incremental=false',
             '--emit-dir', f'lg:{directory}/macro_lg', '--workdir', directory/'macro_work', '-q'], directory/'macro.log', args.timeout)
    settings = {
        'synth.liberty': ROOT/'fake.lib', 'synth.lef': ROOT/'fake.lef', 'synth.sdc': ROOT/'timing.sdc',
        'synth.place': 'true', 'synth.place.fp': 'true', 'synth.place.final': 'true',
        'synth.place.resize': 'false', 'synth.place.io_resynth': 0, 'synth.place.timing_driven': 'false',
        'synth.place.fp.minimum': 100, 'synth.place.fp.colors': 'true', 'synth.place.snapshots': 'true',
        'pass.abc.flow': 'strash; &get -n; &nf; &put',
        'synth.place.iterations': args.iterations, 'synth.place.fp.iterations': args.iterations,
        'synth.place.final.iterations': args.iterations, 'synth.place.bins': 16,
        'lhd.incremental': 'false', 'lhd.jobs': 1,
    }
    settings.update({entry.split('=', 1)[0]: entry.split('=', 1)[1] for entry in args.set})
    # One-macro tiles must qualify as hierarchy candidates (the default of 2 admits only 3+ macros).
    settings['synth.place.fp.min_macros'] = args.min_macros
    if args.snap_mode:
        settings['synth.place.macro.snap_mode'] = args.snap_mode
    cmd = [args.lhd, 'synth', ROOT/(case['name']+'.prp'), f'lg:{directory}/macro_lg',
           '--top', case['name']+'.top', '--seed', '1', '--workdir', directory/'work',
           '--result-json', directory/'result.json', '--emit-dir', f'def:{directory}/physical', '-q']
    for key, value in settings.items():
        cmd += ['--set', f'{key}={value}']
    command(cmd, directory/'flow.log', args.timeout)
    place = directory/'work/synth/place'
    qor = json.loads((place/'qor.json').read_text())
    flow = json.loads((place/'flow.json').read_text())
    candidates = json.loads((place/'macro/fp_candidates.json').read_text())
    if not candidates['started'] or not candidates['completed'] or not candidates['instances']:
        raise ValueError('floorplan planning did not run with physical candidates')
    if not qor['legal'] or flow['mode'] != 'floorplan-macro-cells':
        raise ValueError(f'missing legal three-stage placement: {qor}, {flow}')
    for key in ('hpwl_um', 'wns', 'tns'):
        if not isinstance(qor.get(key), (int, float)) or not math.isfinite(qor[key]):
            raise ValueError(f'missing finite {key}: {qor}')
    units, die, cells = render.read_def(place/'out.def')
    macros = sum(c['master'] == 'REG_MACRO' for c in cells.values())
    standard_cells = len(cells)-macros
    if macros != len(points) or not 10000 <= standard_cells <= 110000:
        raise ValueError(f'fixture size outside intended range: {standard_cells} cells, {macros} macros')
    _, _, macro_cells = render.read_def(place/'macro/out.def')
    for name, cell in cells.items():
        if cell['master'] == 'REG_MACRO':
            previous = macro_cells[name]
            if cell['status'] != 'FIXED' or any(cell[k] != previous[k] for k in ('x', 'y', 'orientation')):
                raise ValueError(f'macro handoff moved or unlocked {name}')
    _, _, exported = render.read_def(directory/'physical/out.def')
    if exported != cells:
        raise ValueError('exported DEF differs from the measured final placement')
    # Every view uses actual stage coordinates and the retained physical IDs.
    stages = [('fp_plan', place/'macro/fp_gp.json'), ('macro_plan', place/'macro/out.def'),
              ('before_legalization', place/'gp.json'), ('after_legalization', place/'after_legalization.def'),
              ('final', place/'out.def')]
    views = {}
    for name, source in stages:
        if source.suffix == '.json':
            snapshot = json.loads(source.read_text())
            positions = {row[0]: (row[1], row[2]) for row in snapshot['nodes']}
            render.actual(case, units, die, cells, directory/(name+'.png'), positions)
            views[name] = render.scores(case, points, edges, cells, positions)
        else:
            stage_units, stage_die, stage_cells = render.read_def(source)
            render.actual(case, stage_units, stage_die, stage_cells, directory/(name+'.png'))
            views[name] = render.scores(case, points, edges, stage_cells)
    metrics = dict(name=case['name'], standard_cells=standard_cells, macros=macros, seconds=time.monotonic()-started,
                   hpwl_um=qor['hpwl_um'], wns=qor['wns'], tns=qor['tns'], time_unit=qor.get('time_unit'),
                   legal=qor['legal'], gp_converged=qor['gp_converged'], stages=views,
                   provenance={p.name: render.digest(p) for p in [Path(args.lhd), ROOT/(case['name']+'.prp'), ROOT/'macros.v', ROOT/'fake.lib', ROOT/'fake.lef', ROOT/'timing.sdc', ROOT/'run.py', ROOT/'render.py']},
                   settings={k:str(v) for k,v in settings.items()})
    if render.digest(args.lhd) != binary_hash:
        raise ValueError('lhd binary changed during measurement; rebuild first, then rerun')
    if args.max_pattern_rms is not None and views['final']['pattern_rms_pitch'] > args.max_pattern_rms:
        metrics['error'] = f'pattern RMS exceeds {args.max_pattern_rms}: {views["final"]}'
    (directory/'metrics.json').write_text(json.dumps(metrics, indent=2)+'\n')
    return metrics


def report(output, results):
    sections = []
    for item in results:
        name = item['name']
        if 'standard_cells' not in item:
            (output/name/'metrics.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><p>FAILED: '+html.escape(item['error'])+'</p></html>')
            sections.append(f'<section><h2>{name}</h2><p class="error">FAILED: {html.escape(item["error"])}</p></section>')
            continue
        pictures = ''.join(f'<figure><img src="{name}/{view}.png" alt="{name}: {view}"><figcaption>{view}</figcaption></figure>'
                           for view in ['expected', 'fp_plan', 'macro_plan', 'before_legalization', 'after_legalization', 'final'])
        metric_html = f'''<p>{item['standard_cells']:,} standard cells; {item['macros']} macros.
          HPWL <b>{item['hpwl_um']:,.1f} µm</b>; WNS <b>{item['wns']:.4g} {html.escape(str(item['time_unit']))}</b>;
          TNS <b>{item['tns']:.4g} {html.escape(str(item['time_unit']))}</b>.
          Pattern RMS {item['stages']['final']['pattern_rms_pitch']:.3f} pitches;
          normalized edge-length CV {item['stages']['final']['edge_length_cv']:.3f}.
          Legal: {item['legal']}; GP converged: {item['gp_converged']}.</p>'''
        (output/name/'metrics.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><style>body{font:15px system-ui;color:#23344b}</style>'+metric_html+'</html>')
        failure = '<p class="error">FAILED: '+html.escape(item['error'])+'</p>' if 'error' in item else ''
        sections.append(f'''<section id="{name}"><h2>{name}</h2>{failure}
          <p>{item['standard_cells']:,} standard cells; {item['macros']} macros; {item['seconds']:.1f}s.
          Legal: {item['legal']}; GP converged: {item['gp_converged']}.</p>
          {metric_html}
          <p><a href="{name}/metrics.json">Metrics and input hashes</a> · <a href="{name}/result.json">Flow report</a> ·
          <a href="{name}/work/synth/place/flow.json">Stage handoff</a> · <a href="{name}/physical/out.def">Final DEF</a></p>
          <div class="views">{pictures}</div></section>''')
    (output/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
      <title>Placement regularity — measured results</title><style>
      body{font:16px system-ui;margin:2em;color:#24334a;background:#f7f9fc}section{background:white;padding:1em;margin:2em 0}
      .views{display:grid;grid-template-columns:repeat(3,minmax(220px,1fr));gap:1em}figure{margin:0}img{width:100%}
      .error{color:#a21b28} @media(max-width:800px){.views{grid-template-columns:1fr}}</style>
      <h1>Placement regularity — measured results</h1>
      <p>Expected views are idealized topology sketches. Other PNGs use actual flow coordinates.
      Colors identify tile ownership; dark outlines identify hard macros. White space and skew are observable results.
      The invented Liberty/LEF and placement RC estimate are for comparisons within this suite, not silicon signoff.</p>
      <p>Lower pattern RMS (similarity fit allowing rotation/reflection, in grid pitches) and adjacent-edge length CV
      indicate a more regular macro frame. Timing uses a 20ns clock; no timing-closure threshold is imposed.
      Legal placement and finite HPWL/WNS/TNS are required. Pattern thresholds are opt-in.</p>'''+''.join(sections)+'</html>')
    (output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['all']+[c['name'] for c in CASES], default='all')
    parser.add_argument('--lhd', default='lhd/lhd' if os.environ.get('TEST_SRCDIR') else str(Path(os.environ.get('BUILD_WORKSPACE_DIRECTORY', '.'))/'bazel-bin/lhd/lhd'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--iterations', type=int, default=1500)
    parser.add_argument('--min-macros', type=int, default=0,
                        help='synth.place.fp.min_macros: a hierarchy candidate needs more macros than this; 0 makes each one-macro tile a candidate')
    parser.add_argument('--set', action='append', default=[], help='additional lhd setting KEY=VALUE')
    parser.add_argument('--snap-mode', choices=['alignment', 'symmetry'], help='synth.place.macro.snap_mode (flow default: alignment)')
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--max-pattern-rms', type=float)
    args = parser.parse_args()
    if args.iterations < 1 or args.timeout < 1 or (args.max_pattern_rms is not None and (not math.isfinite(args.max_pattern_rms) or args.max_pattern_rms < 0)):
        parser.error('iterations/timeout must be positive and pattern threshold must be finite and nonnegative')
    args.lhd = str(Path(args.lhd).resolve())
    output = (args.output or Path(os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR', 'repros/placement_regularity'))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    previous = output/'summary.json'
    results = {r['name']: r for r in json.loads(previous.read_text())} if previous.exists() else {}
    failed = False
    for case in CASES:
        if args.case not in ('all', case['name']):
            continue
        print(f'Running {case["name"]}: {output}', flush=True)
        try:
            results[case['name']] = run(case, args, output)
        except (RuntimeError, ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
            results[case['name']] = dict(name=case['name'], error=str(error))
            print(str(error), file=sys.stderr, flush=True)
        failed |= 'error' in results[case['name']]
        report(output, [results[c['name']] for c in CASES if c['name'] in results])
    print(f'Report: {output}/index.html', flush=True)
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
