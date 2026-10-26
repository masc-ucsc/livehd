#!/usr/bin/env python3
"""Paired macro magnet screens using repository-owned sparse/dense meshes.

Full physical placement and final STA run in each arm. The optional perf prefix
is supplied by the caller so profiling and resource limits remain explicit.
No sibling benchmark inputs or external simulator are needed.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import subprocess
import time

from generate import CASES, source

ROOT = Path(__file__).resolve().parent


def execute(command, directory, timeout):
    (directory / 'command.json').write_text(json.dumps([str(v) for v in command], indent=2) + '\n')
    start = time.monotonic()
    with (directory / 'flow.log').open('w') as log:
        subprocess.run([str(v) for v in command], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=timeout)
    return time.monotonic() - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lhd', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--iterations', type=int, default=1500)
    parser.add_argument('--case', choices=['all', 'mesh_2x2_sparse', 'mesh_4x4_sparse', 'mesh_2x2_dense'], default='all')
    parser.add_argument('--arm', choices=['both', 'off', 'on'], default='both')
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--profile', action='store_true', help='record perf stat and 49-Hz call stacks separately for each arm')
    parser.add_argument('--sparse-width', type=int, choices=[1, 32], default=32,
                        help='active bits in sparse mesh logic; physical macro interfaces remain 32 bits')
    args = parser.parse_args()
    args.lhd = args.lhd.resolve()
    if args.iterations < 1 or args.timeout < 1:
        parser.error('iterations and timeout must be positive')
    args.output = args.output.resolve()
    inputs = args.output / 'inputs'
    inputs.mkdir(parents=True, exist_ok=True)
    binary_hash = hashlib.sha256(args.lhd.read_bytes()).hexdigest()
    macro_lg = inputs / 'macro_lg'
    if not macro_lg.exists():
        execute([args.lhd, 'compile', ROOT / 'macros.v', '--top', 'REG_MACRO',
                 '--set', 'compile.slang.blackbox_attribute=true', '--set', 'lhd.incremental=true',
                 '--emit-dir', f'lg:{macro_lg}', '--workdir', inputs / 'macro_work', '-q'], inputs, args.timeout)
    cases = []
    for name, side, sparse in [('mesh_2x2_sparse', 2, True), ('mesh_4x4_sparse', 4, True), ('mesh_2x2_dense', 2, False)]:
        original = next(c for c in CASES if c['name'] == f'mesh_{side}x{side}')
        case = dict(original, name=name, depth=1 if sparse else original['depth'])
        if args.case in ('all', name):
            cases.append(case)
    rows = []
    for case in cases:
        rtl = inputs / (case['name'] + '.prp')
        text = source(case)
        if case['name'].endswith('_sparse') and args.sparse_width == 1:
            # Exercise a real sparse corridor: one active data lane, while
            # retaining the same hard-macro body and 32-bit imported interface.
            text = text.replace(f'U{32 * case["side"] ** 2}', f'U{case["side"] ** 2}')
            text = text.replace('U32', 'U1').replace('((x << 5) | (x >> 27))#[0..<32]', 'x')
            text = text.replace('q = hard::[name=macro](D=state)', 'q = hard::[name=macro](D=state)#[0]')
            text = re.sub(r'salt=salt \^ (\d+)', lambda m: 'salt=salt ^ ' + str(int(m[1]) & 1), text)
        rtl.write_text(text)
        for arm in ('off', 'on') if args.arm == 'both' else (args.arm,):
            directory = args.output / case['name'] / arm
            directory.mkdir(parents=True, exist_ok=False)
            settings = {
                'synth.liberty': ROOT / 'fake.lib', 'synth.lef': ROOT / 'fake.lef', 'synth.sdc': ROOT / 'timing.sdc',
                'synth.place': 'true', 'synth.place.fp': 'false', 'synth.place.final': 'true',
                'synth.place.resize': 'false', 'synth.place.io_resynth': 0, 'synth.place.timing_driven': 'false',
                'synth.place.snapshots': 'true', 'synth.place.macro.magnet': str(arm == 'on').lower(),
                'synth.place.macro.legalize_cells': 'false', 'synth.place.macro.detailed': 'false',
                'synth.place.final.independent_timing': 'true',
                'synth.place.iterations': args.iterations, 'synth.place.final.iterations': args.iterations,
                'synth.place.grid.density': 16, 'lhd.incremental': 'true', 'lhd.jobs': 1,
                'pass.abc.flow': 'strash; &get -n; &nf; &put',
            }
            command = [args.lhd, 'synth', rtl, f'lg:{macro_lg}', '--top', case['name'] + '.top', '--seed', '1',
                       '--workdir', directory / 'work', '--result-json', directory / 'result.json', '-q']
            for key, value in settings.items():
                command += ['--set', f'{key}={value}']
            if args.profile:
                command = ['perf', 'stat', '-o', directory / 'perf.stat',
                           '-e', 'task-clock,cycles,instructions,cache-references,cache-misses', '--',
                           'perf', 'record', '-z3', '-F', '49', '--call-graph', 'dwarf,4096',
                           '-o', directory / 'perf.data', '--', *command]
            seconds = execute(command, directory, args.timeout)
            place = directory / 'work/synth/place'
            qor = json.loads((place / 'qor.json').read_text())
            flow = json.loads((place / 'flow.json').read_text())
            row = {'case': case['name'], 'arm': arm, 'seconds': seconds, 'qor': qor, 'flow': flow,
                   'settings': {k: str(v) for k, v in settings.items()}, 'binary_sha256': binary_hash,
                   'sparse_active_bits': args.sparse_width}
            if arm == 'on':
                report = json.loads((place / 'magnet.json').read_text())
                row['magnet'] = {'groups': len(report['groups']), 'rounds': sum(r['status'] == 'merged' for r in report['rounds']),
                                 'frames': len(report['frames']), 'stopped_settled': report['stopped_settled']}
            (directory / 'metrics.json').write_text(json.dumps(row, indent=2) + '\n')
            rows.append(row)
            (args.output / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
            print(json.dumps({k: row[k] for k in ('case', 'arm', 'seconds')}), flush=True)
            if hashlib.sha256(args.lhd.read_bytes()).hexdigest() != binary_hash:
                raise RuntimeError('executable changed during comparison')
    table = []
    for row in rows:
        q = row['qor']
        animation = f"{row['case']}/{row['arm']}/work/synth/place/macro_animation.html"
        table.append(f"<tr><td>{html.escape(row['case'])}</td><td>{row['arm']}</td><td>{row['seconds']:.2f}</td>"
                     f"<td>{q['hpwl_um']:.2f}</td><td>{q.get('wns')}</td><td>{q.get('tns')}</td><td>{q['legal']}</td>"
                     f"<td>{row.get('magnet', {}).get('groups', 0)}</td><td>"
                     + (f'<a href="{animation}">Playback</a>' if row['arm'] == 'on' else '') + '</td></tr>')
    (args.output / 'comparison.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Macro magnet regularity measurements</title><style>body{font:15px system-ui;margin:2em}td,th{padding:6px;text-align:right}table{border-collapse:collapse}tr{border-bottom:1px solid #ccc}</style>
<h1>Macro magnet regularity measurements</h1><p>Repository-owned invented technology, FP disabled, macro and final placement enabled. Sparse meshes use one logic layer; the dense mesh retains the existing 28-layer fixture. Timing units follow the QoR JSON. These measurements do not claim routed parity.</p>
<table><tr><th>Case</th><th>Magnet</th><th>Seconds</th><th>HPWL µm</th><th>WNS</th><th>TNS</th><th>Legal</th><th>Groups</th><th>Frames</th></tr>'''
        + ''.join(table) + '</table><p><a href="summary.json">Full settings and measurements</a></p></html>')
    print('Report:', args.output / 'comparison.html')


if __name__ == '__main__':
    main()
