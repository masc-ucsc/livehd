# SPDX-License-Identifier: BSD-3-Clause
"""Explicit post-route adapter for a retained ORFS job directory.

Consumes environment.json and completed DEF/DRC artifacts. It never starts a
router, repairs placement or trains. Missing control metadata is an error;
clock/reset roots are not guessed from names. See README for the harness hook.
"""
import argparse
import gzip
import json
import math
from pathlib import Path
import shlex
import time

from .ingest import ingest
from .pool import _atomic, canonical, sha


def paths(value, base=None):
    if isinstance(value, list) and all(isinstance(p, str) for p in value):
        result = [Path(p) for p in value]
        return [(base / p if base is not None and not p.is_absolute() else p) for p in result]
    if not isinstance(value, str) or '{' in value or '}' in value:
        raise ValueError('ORFS file lists must use JSON arrays or shell-style quoting; use explicit --lef/--liberty overrides')
    result = [Path(p) for p in shlex.split(value)]
    return [(base / p if base is not None and not p.is_absolute() else p) for p in result]


def physical_input(job, environment):
    path = Path(environment['INPUT_PHYSICAL'])
    return path if path.is_absolute() else job / path


def controls(job, environment, override):
    candidates = [override] if override else [job / 'uroute_controls.json', job / 'W/synth/place/qor.json']
    if not override and environment.get('INPUT_PHYSICAL'):
        candidates.append(physical_input(job, environment).parent / 'qor.json')
    for path in candidates:
        if path and path.is_file():
            doc = json.loads(path.read_bytes())
            if isinstance(doc, dict) and isinstance(doc.get('prects_controls'), dict):
                return path.resolve()
    raise ValueError('no clock/reset control metadata; provide --controls or job/uroute_controls.json with prects_controls and provenance')


def combined_liberty(job, inputs):
    if not inputs:
        raise ValueError('ORFS job has no Liberty inputs')
    contents = []
    for path in inputs:
        if not path.is_file():
            raise ValueError(f'missing retained Liberty input: {path}; provide --liberty')
        contents.append(gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes())
    data = b'\n'.join(contents)
    directory = job / 'uroute_inputs'
    directory.mkdir(exist_ok=True)
    path = directory / (sha(data) + '.lib')
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError('immutable combined Liberty input changed')
    else:
        _atomic(path, data)
    return path


def postroute(args):
    started = time.monotonic()
    job = Path(args.job).resolve()
    environment_path = job / 'environment.json'
    environment = json.loads(environment_path.read_bytes())
    control_path = controls(job, environment, args.controls)
    if getattr(args, 'odb', None) is not None:
        definition = None
    elif args.definition:
        definition = args.definition.resolve()
    else:
        definition = next((job / 'results' / name for name in ('6_final.def', '6_final.def.gz')
                           if (job / 'results' / name).is_file()), None)
    if getattr(args, 'odb', None) is None and (definition is None or not definition.is_file()):
        raise ValueError('no final routed DEF; provide --def explicitly for an unrouted result')
    status = args.status
    if status == 'auto':
        state_path = job / 'status.json'
        state = json.loads(state_path.read_bytes()) if state_path.exists() else {}
        if state.get('status') != 'completed':
            raise ValueError('job is not complete; assert --status routed or --status unrouted explicitly')
        status = 'routed'
    if status == 'unrouted' and args.last_global_overflow is None and getattr(args, 'odb', None) is None and getattr(args, 'overflow_unavailable', None) is None:
        raise ValueError('unrouted result requires --last-global-overflow or an explicit unavailability reason; no zero map is fabricated')
    lefs = args.lef or (paths(environment.get('TECH_LEF', ''), job) + paths(environment.get('SC_LEF', ''), job)
                        + paths(environment.get('ADDITIONAL_LEFS', ''), job))
    if not lefs or any(not path.is_file() for path in lefs):
        raise ValueError('missing retained LEF inputs; provide explicit --lef paths')
    liberty = args.liberty.resolve() if args.liberty else combined_liberty(job, paths(environment.get('LIB_FILES', ''), job))
    drc = args.drc or job / 'reports/5_route_drc.rpt'
    if status == 'routed' and not drc.is_file():
        raise ValueError('routed job has no DRC report; provide --drc (an empty file explicitly means clean)')
    livehd_export = args.livehd_export
    if livehd_export is None and args.source == 'livehd':
        candidates = [job / 'W/synth/place/uroute_export']
        if environment.get('INPUT_PHYSICAL'):
            candidates.append(physical_input(job, environment).parent / 'uroute_export')
        livehd_export = next((p for p in candidates if (p / 'metadata.json').is_file()), None)
    utilization = args.utilization
    if utilization is None and livehd_export is None:
        try:
            utilization = float(environment['CORE_UTILIZATION']) / 100
        except (ValueError, TypeError, KeyError) as error:
            raise ValueError('no target utilization in export/environment; provide --utilization') from error
    if utilization is not None and (not math.isfinite(utilization) or not 0 <= utilization <= 1):
        raise ValueError('target utilization must be in 0..1')
    options = argparse.Namespace(definition=definition, lef=lefs, liberty=liberty, controls=control_path,
                                 pool=args.pool, placement_metrics=args.placement_metrics, native=args.native, drc=drc,
                                 livehd_export=livehd_export, last_global_overflow=args.last_global_overflow,
                                 source=args.source, status=status, utilization=utilization, bins=args.bins,
                                 wirelength_report=args.wirelength_report, odb=getattr(args, 'odb', None),
                                 openroad=getattr(args, 'openroad', None), odb_timeout=getattr(args, 'odb_timeout', 300),
                                 overflow_unavailable=getattr(args, 'overflow_unavailable', None))
    sample = ingest(options)
    result = {'status': 'imported', 'sample_id': sample['id'], 'sample_status': sample['status'],
              'pool': str(args.pool.resolve()), 'environment_sha256': sha(environment_path.read_bytes()),
              'controls': str(control_path), 'controls_sha256': sha(control_path.read_bytes()),
              'utilization_source': 'explicit' if args.utilization is not None else 'LiveHD export' if livehd_export else 'ORFS CORE_UTILIZATION',
              'seconds': time.monotonic() - started}
    _atomic(job / 'uroute_ingest.json', canonical(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job', type=Path, required=True)
    parser.add_argument('--pool', type=Path, required=True)
    parser.add_argument('--source', choices=('livehd', 'external'), required=True)
    parser.add_argument('--controls', type=Path)
    physical = parser.add_mutually_exclusive_group()
    physical.add_argument('--def', dest='definition', type=Path)
    physical.add_argument('--odb', type=Path)
    parser.add_argument('--openroad', type=Path)
    parser.add_argument('--odb-timeout', type=float, default=300)
    parser.add_argument('--lef', action='append', type=Path)
    parser.add_argument('--liberty', type=Path)
    parser.add_argument('--drc', type=Path)
    parser.add_argument('--livehd-export', type=Path)
    parser.add_argument('--wirelength-report', type=Path)
    parser.add_argument('--status', choices=('auto', 'routed', 'unrouted'), default='auto')
    parser.add_argument('--last-global-overflow', type=Path)
    parser.add_argument('--overflow-unavailable')
    parser.add_argument('--utilization', type=float)
    parser.add_argument('--bins', type=int)
    parser.add_argument('--placement-metrics', type=Path, default=Path('bazel-bin/tools/placement_metrics'))
    parser.add_argument('--native', type=Path, default=Path('bazel-bin/tools/uroute/native'))
    args = parser.parse_args()
    try:
        result = postroute(args)
    except (ValueError, OSError) as error:
        parser.exit(2, f'uroute postroute: {error}\n')
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
