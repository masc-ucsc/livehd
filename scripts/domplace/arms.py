#!/usr/bin/env python3
"""Matched three-arm domplace experiments (todo/livehd/2d-domplace.html, H).

Arm 1 is the ABC all-CMOS baseline, arm 2 the USYN CMOS realization and arm 3
USYN with the provisional domino profile. Every arm pins the same placement
knobs (seed, iterations, density, grid density, no timing-driven placement,
resizing or resynthesis, io_resynth=0, OpenTimer off) and times its final
placement with the same native stage timer (synth.place.stage_timing).
Arms 2 and 3 pin tmap_trials=1, so the domino arm keeps arm 2's USYN trial.
The core is area-driven (one utilization) and/or fixed (the core and die of
the arm-1 area-driven run); the two are reported separately.

Runs are serialized. Each records wall time and the peak RSS of its whole
process tree (sampled from /proc), LEC against the RTL (gensim cell models,
plus the bundle models for arm 3), a status class (ok, unproven,
unsupported, infeasible, timeout, failed), coverage, both area bases (rail
inverters counted once), two-row legality and ECO blocks, the stage period
and classes, C_clk, HPWL and sizing counts. --warm reruns each leg twice in
its workdir and records the second. Output (default repros/domplace_<date>/):
manifest.json (HEAD, dirty-diff hash, lhd hash, input hashes, options,
commands), results.json, results.tsv, and per leg its workdir, DEF/LEF and
reports. --dry-run writes only the manifest.

Stage coefficients have no built-in values: pass them to arm 3 explicitly
(--arm3-set pass.usyn.domino_eval_fo4=2 ...), or its stage timing reports
the unmeasured classes and status incomplete.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).absolute().parent))
import domplace_view  # noqa: E402

ARMS = {
    1: ('abc-cmos', ['synth.mapper=abc']),
    2: ('usyn-cmos', ['synth.mapper=usyn', 'pass.usyn.clock_phases=1', 'pass.usyn.tmap=abc', 'pass.usyn.tmap_trials=1']),
    3: ('usyn-domino', ['synth.mapper=usyn', 'pass.usyn.clock_phases=1', 'pass.usyn.tmap=abc', 'pass.usyn.tmap_trials=1',
                        'pass.usyn.physical=domino_provisional']),
}
INFEASIBLE = ('multirow packing cannot anchor', 'two-row ECO cannot', 'Multirow_failure', 'does not fit core')


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args):
    try:
        return subprocess.run(['git', *args], capture_output=True, text=True, timeout=30, check=True).stdout
    except (OSError, subprocess.SubprocessError):
        return None


def tree_rss_kib(pid):
    """Resident KiB of pid and its descendants (/proc), 0 once it is gone."""
    total, stack = 0, [pid]
    while stack:
        current = stack.pop()
        try:
            for line in Path(f'/proc/{current}/status').read_text().splitlines():
                if line.startswith('VmRSS:'):
                    total += int(line.split()[1])
            for task in Path(f'/proc/{current}/task').iterdir():
                stack.extend(int(child) for child in (task / 'children').read_text().split())
        except (OSError, ValueError):
            continue
    return total


def measured(command, timeout, log):
    """Run command; return (returncode or None on timeout, wall seconds, peak tree RSS MiB)."""
    start = time.monotonic()
    with open(log, 'w') as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
        peak, done = [0], threading.Event()

        def sample():
            while not done.is_set():
                peak[0] = max(peak[0], tree_rss_kib(process.pid))
                done.wait(0.05)
        sampler = threading.Thread(target=sample, daemon=True)
        sampler.start()
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = None
        done.set()
        sampler.join()
    return code, time.monotonic() - start, peak[0] / 1024


def sets(pairs):
    out = []
    for pair in pairs:
        out += ['--set', pair]
    return out


def common_sets(args, core):
    pins = [f'synth.liberty={args.liberty}', f'synth.lef={args.lef}', f'synth.sdc={args.sdc}', 'synth.place=true',
            'synth.place.timing_driven=false', 'synth.place.resize=false', 'synth.place.resynth=false',
            'synth.place.io_resynth=0', 'synth.opentimer=false', 'synth.place.stage_timing=true',
            f'synth.place.iterations={args.iterations}', f'synth.place.density={args.density}',
            f'synth.place.grid.density={args.grid_density}']
    if args.clock_port:
        pins.append(f'synth.place.stage_timing.clock_port={args.clock_port}')
    if core == 'area':
        pins.append(f'synth.place.utilization={args.utilization}')
    return pins + list(args.set or [])


def legs(args):
    """(name, arm, core, sets) in run order. The fixed-core legs take the core
    and die of the arm-1 area-driven run, so it runs first, also when only
    the fixed core is requested."""
    arms = sorted({int(a) for a in args.arms.split(',')})
    if not set(arms) <= set(ARMS):
        sys.exit(f'arms.py: unknown arm in {args.arms}')
    area = arms if args.core_mode in ('area', 'both') else []
    fixed = arms if args.core_mode in ('fixed', 'both') else []
    if fixed and 1 not in arms:
        sys.exit('arms.py: the fixed-core legs take the core of the arm-1 area-driven run; include arm 1')
    if fixed and not area:
        area = [1]
    out = []
    for core, members in (('area', area), ('fixed', fixed)):
        for arm in members:
            extra = list(args.arm3_set or []) if arm == 3 else []
            out.append((f'arm{arm}-{core}', arm, core, common_sets(args, core) + ARMS[arm][1] + extra))
    if args.sta_leg:  # U16: one report-only OpenTimer run of arm 2
        out.append(('arm2-sta', 2, 'area', [s for s in common_sets(args, 'area') if s != 'synth.opentimer=false']
                    + ['synth.opentimer=true'] + ARMS[2][1]))
    return out


def fixed_core(view, lefs):
    """The core (the rows' extent) and die of a placed DEF, in microns."""
    sites = domplace_view.lef_sites(lefs)
    xl = min(r.x for r in view.row_list)
    yl = min(r.y for r in view.row_list)
    xh = max(r.x + r.count * r.step for r in view.row_list)
    yh = max(r.y + round(sites[r.site][1] * view.dbu) for r in view.row_list)
    core = ','.join(f'{v / view.dbu:g}' for v in (xl, yl, xh, yh))
    die = ','.join(f'{v / view.dbu:g}' for v in view.die)
    return core, die


def leg_metrics(args, leg_dir, result, arm):
    qor = result.get('qor', {})
    place = qor.get('place', {})
    metrics = {'legal': place.get('legal'), 'hpwl_um': place.get('hpwl_um'), 'multirow': place.get('multirow'),
               'multirow_eco': place.get('multirow_eco'), 'final_rudy': None,
               'reused_final_state': bool(place.get('reused_final_state'))}
    physical = qor.get('usyn', {}).get('physical')
    metrics['coverage'] = physical.get('totals') if physical else None
    sizing = place.get('domino_sizing')
    # qor.place.multirow_eco is only the last ECO place() call's; the sizing
    # block sums the two-row ECO over its accepted trials.
    metrics['sizing'] = {'counts': sizing.get('counts'), 'variants': sizing.get('variants'),
                         'clock': sizing.get('clock'), 'eco': sizing.get('eco')} if sizing else None
    stage = leg_dir / 'W/synth/place/stage_timing.json'
    if stage.is_file():
        report = json.loads(stage.read_text())
        metrics['stage_timing'] = {'status': report['status'], 'period_ps': report['period']['estimate_ps'],
                                   'limiting': report['period']['limiting'], 'reasons': report['reasons'],
                                   'classes': report['classes'], 'c_clk_ff': report['clock_accounting']['c_clk_ff'],
                                   'generation': report['model']['generation'],
                                   'producer_salt': (report['model']['provisional'] or {}).get('producer_salt')}
    def_dir = leg_dir / 'def'
    if (def_dir / 'out.def').is_file():
        lefs = [def_dir / lef for lef in json.loads((def_dir / 'lefs.json').read_text())]  # relative to the DEF
        bundle = leg_dir / 'W/synth/qor.json.domino/domino_models.json'
        metrics['area'] = domplace_view.area_report(domplace_view.read_def(def_dir / 'out.def'), domplace_view.lef_sizes(lefs),
                                                    domplace_view.liberty_areas(args.liberty.split(',')),
                                                    bundle if arm == 3 and bundle.is_file() else None)
    return metrics


def run_leg(args, out, name, arm, command, cells):
    leg_dir = out / 'runs' / name
    leg_dir.mkdir(parents=True, exist_ok=True)
    full = command + ['--workdir', str(leg_dir / 'W'), '--emit-dir', f'def:{leg_dir}/def',
                      '--result-json', str(leg_dir / 'result.json')]
    code, wall, rss = measured(full, args.timeout, leg_dir / 'lhd.log')
    entry = {'leg': name, 'arm': arm, 'arm_name': ARMS[arm][0], 'wall_s': wall, 'peak_rss_mib': rss}
    result = json.loads((leg_dir / 'result.json').read_text()) if (leg_dir / 'result.json').is_file() else {}
    if code is None:
        entry['status'] = 'timeout'
        return entry, result
    error = result.get('error')
    if code != 0 or error:
        message = (error or {}).get('message', '')
        entry['error'] = error
        entry['status'] = ('infeasible' if any(n in message for n in INFEASIBLE)
                           else 'unsupported' if (error or {}).get('class') in ('usage', 'unsupported') else 'failed')
        return entry, result
    entry.update(leg_metrics(args, leg_dir, result, arm))
    if args.warm:  # measured twice in the same workdir; the second counts
        for _ in range(2):
            code, wall, rss = measured(full, args.timeout, leg_dir / 'lhd.warm.log')
        warm = json.loads((leg_dir / 'result.json').read_text())
        entry['warm'] = {'wall_s': wall, 'peak_rss_mib': rss, 'ok': code == 0,
                         'reused_final_state': bool(warm.get('qor', {}).get('place', {}).get('reused_final_state'))}
    if args.lec:
        lec = [args.lhd, 'lec', '--impl', f'lg:{leg_dir}/W/synth/net', '--ref', f'verilog:{args.rtl}', '--lib', f'lg:{cells}',
               '--top', args.top, '--set', 'formal.effort=proof', '--set', f'formal.timeout={args.lec_timeout}',
               '--workdir', str(leg_dir / 'lec'), '--result-json', str(leg_dir / 'lec.json')]
        if arm == 3:
            lec += ['--lib', f'lg:{leg_dir}/W/synth/qor.json.domino/models']
        measured(lec, args.timeout, leg_dir / 'lec.log')
        proof = json.loads((leg_dir / 'lec.json').read_text()).get('lec', {}) if (leg_dir / 'lec.json').is_file() else {}
        entry['lec'] = proof.get('verdict', 'failed')
        entry['status'] = 'ok' if entry['lec'] == 'proven-full' and not proof.get('bounded') else 'unproven'
    else:
        entry['lec'] = 'skipped'
        entry['status'] = 'unproven'
    return entry, result


TSV = ['leg', 'arm_name', 'status', 'lec', 'wall_s', 'peak_rss_mib', 'converted', 'eligible', 'lef_um2', 'domino_lef_um2',
       'liberty_um2', 'legal', 'multirow_cells', 'eco_displaced', 'hpwl_um', 'stage_status', 'period_ps', 'limiting',
       'c_clk_ff', 'sizing_trials', 'warm_wall_s', 'warm_rss_mib']


def tsv_row(e):
    coverage, area, stage = e.get('coverage') or {}, e.get('area') or {}, e.get('stage_timing') or {}
    sizing, warm = (e.get('sizing') or {}).get('counts') or {}, e.get('warm') or {}
    eco = (e.get('sizing') or {}).get('eco') or e.get('multirow_eco') or {}
    values = [e['leg'], e['arm_name'], e['status'], e.get('lec'), e['wall_s'], e['peak_rss_mib'], coverage.get('converted'),
              coverage.get('eligible'), area.get('lef_um2'), area.get('domino_lef_um2'), area.get('liberty_um2'), e.get('legal'),
              (e.get('multirow') or {}).get('cells'), eco.get('displaced_cells'), e.get('hpwl_um'),
              stage.get('status'), stage.get('period_ps'), stage.get('limiting'), stage.get('c_clk_ff'), sizing.get('trials'),
              warm.get('wall_s'), warm.get('peak_rss_mib')]
    return '\t'.join('' if v is None else f'{v:.4f}' if isinstance(v, float) else str(v) for v in values)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--rtl', required=True, help='one Verilog source (also the LEC reference)')
    parser.add_argument('--top', required=True)
    parser.add_argument('--liberty', required=True, help='comma-separated Liberty files')
    parser.add_argument('--lef', required=True, help='comma-separated technology and cell LEFs')
    parser.add_argument('--sdc', required=True)
    parser.add_argument('--lhd', default='bazel-bin/lhd/lhd')
    parser.add_argument('--out', help='default repros/domplace_<date>')
    parser.add_argument('--arms', default='1,2,3')
    parser.add_argument('--core-mode', choices=('area', 'fixed', 'both'), default='both')
    parser.add_argument('--utilization', default='0.30')
    parser.add_argument('--seed', default='1')
    parser.add_argument('--iterations', default='1500')
    parser.add_argument('--density', default='0.5')
    parser.add_argument('--grid-density', default='64')
    parser.add_argument('--clock-port', help='synth.place.stage_timing.clock_port for every arm')
    parser.add_argument('--set', action='append', help='KEY=VALUE for every arm')
    parser.add_argument('--arm3-set', action='append', help='KEY=VALUE for arm 3 only (stage coefficients, domino_clock)')
    parser.add_argument('--sta-leg', action='store_true', help='add one report-only OpenTimer run of arm 2')
    parser.add_argument('--warm', action='store_true', help='rerun each leg twice in its workdir; record the second')
    parser.add_argument('--no-lec', dest='lec', action='store_false')
    parser.add_argument('--lec-timeout', default='60')
    parser.add_argument('--timeout', type=float, default=3600, help='seconds per lhd invocation')
    parser.add_argument('--dry-run', action='store_true', help='write the manifest and commands only')
    args = parser.parse_args()
    for path in [args.rtl, args.sdc] + args.liberty.split(',') + args.lef.split(','):
        if not Path(path).is_file():
            sys.exit(f'arms.py: missing input {path}')
    out = Path(args.out or f'repros/domplace_{datetime.date.today():%Y%m%d}')
    out.mkdir(parents=True, exist_ok=True)
    plan = legs(args)
    command = [args.lhd, 'synth', args.rtl, '--top', args.top, '-q', '--seed', args.seed]
    diff = git('diff', 'HEAD')
    lhd = Path(args.lhd)
    manifest = {
        'schema': 1, 'kind': 'domplace_arms', 'created': datetime.datetime.now().isoformat(timespec='seconds'),
        'dry_run': args.dry_run,
        'livehd': {'head': (git('rev-parse', 'HEAD') or '').strip() or None,
                   'dirty_diff_sha256': hashlib.sha256(diff.encode()).hexdigest() if diff else None,
                   'lhd': str(lhd), 'lhd_sha256': sha256(lhd) if lhd.is_file() else None,
                   'salts': 'compiled into lhd (lhd_sha256); each leg records the bundle producer_salt and the stage '
                            'timing model generation'},
        'inputs': {str(p): sha256(p) for p in [args.rtl, args.sdc] + args.liberty.split(',') + args.lef.split(',')},
        'options': {k: v for k, v in vars(args).items() if k not in ('dry_run',)},
        'legs': [{'name': n, 'arm': a, 'arm_name': ARMS[a][0], 'core': c,
                  'command': command + sets(s) + (['<core and die of arm1-area>'] if c == 'fixed' else [])}
                 for n, a, c, s in plan],
        'notes': ['matched: seed, iterations, density, grid density, no timing-driven placement, resizing or resynthesis, '
                  'io_resynth=0, OpenTimer off, native stage timing; tmap_trials=1 in arms 2 and 3',
                  'arm 3 stage coefficients are only those passed with --arm3-set; none is built in',
                  'final RUDY: not reported (plan H3 not implemented)',
                  'HighTide: not integrated (pending decisions U14 and U15)'],
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    if args.dry_run:
        print(f'arms.py: dry run, {len(plan)} legs planned in {out / "manifest.json"}')
        return 0
    cells = out / 'cells'
    if args.lec and not cells.exists():
        subprocess.run([args.lhd, 'pass', 'liberty', 'gensim', *args.liberty.split(','), '--emit-dir', f'lg:{cells}',
                        '--workdir', str(out / 'gensim'), '-q'], check=True, capture_output=True)
    entries, core = [], None
    for name, arm, core_mode, pairs in plan:
        extra = []
        if core_mode == 'fixed':
            if not core:
                entries.append({'leg': name, 'arm': arm, 'arm_name': ARMS[arm][0], 'status': 'failed',
                                'error': {'message': 'the arm-1 area-driven run placed nothing'}, 'wall_s': 0, 'peak_rss_mib': 0})
                continue
            extra = sets([f'synth.place.core={core[0]}', f'synth.place.die={core[1]}'])
        entry, _ = run_leg(args, out, name, arm, command + sets(pairs) + extra, cells)
        entry['core'] = core_mode if not extra else {'mode': 'fixed', 'core_um': core[0], 'die_um': core[1]}
        entries.append(entry)
        print(f'arms.py: {name}: {entry["status"]} in {entry["wall_s"]:.1f} s', flush=True)
        if name == 'arm1-area' and entry['status'] in ('ok', 'unproven'):
            def_dir = out / 'runs' / name / 'def'
            core = fixed_core(domplace_view.read_def(def_dir / 'out.def'),
                              [def_dir / lef for lef in json.loads((def_dir / 'lefs.json').read_text())])
    (out / 'results.json').write_text(json.dumps({'schema': 1, 'legs': entries}, indent=1) + '\n')
    (out / 'results.tsv').write_text('\t'.join(TSV) + '\n' + ''.join(tsv_row(e) + '\n' for e in entries))
    return 0 if all(e['status'] in ('ok', 'unproven') for e in entries) else 1


if __name__ == '__main__':
    sys.exit(main())
