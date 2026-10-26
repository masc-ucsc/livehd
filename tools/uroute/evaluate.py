# SPDX-License-Identifier: BSD-3-Clause
"""Score routed queries with native calibrated URoute and explicit support rules.

No training occurs here. A query never supports itself. Earlier/zero-shot modes
only use samples published before the query. An unseen-design claim additionally
requires complete checkpoint lineage excluding that design, including validation.
"""
import argparse
from functools import lru_cache
import json
import math
from pathlib import Path
import subprocess
import tempfile
import time

from .export_weights import LIMIT
from .maps import read
from .pool import VERSIONS, _atomic, _identity, canonical, inside, lineage_status, load_sample, read_manifest, sha
from .score import METRICS, absolute_channel, defined_mean, metrics


class Pool:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.manifest = read_manifest(root)
        self.manifest_sha256 = sha(canonical(self.manifest))
        self.records = {r['id']: r for r in self.manifest['samples']}
        self.compatible = {}
        for key, record in self.records.items():
            if record.get('status') == 'routed' and all(type(record.get(k)) is int and record[k] == v for k, v in VERSIONS.items()):
                _identity(record)
                self.compatible[key] = record
        weights = self.manifest.get('weights')
        if not isinstance(weights, dict):
            raise ValueError('evaluation requires trained weights; this pool has none')
        self.weights = inside(self.root, weights['file'])
        if self.weights.stat().st_size > LIMIT or sha(self.weights.read_bytes()) != weights['sha256']:
            raise ValueError('evaluation weight digest mismatch')
        self.weights_sha256 = weights['sha256']

    @lru_cache(maxsize=16)
    def load(self, key):
        return load_sample(self.root, self.compatible[key])

    def unseen(self, top):
        provenance = self.manifest.get('provenance', {})
        status = lineage_status(provenance, top)
        if status == 'incomplete':
            return {'verified': False, 'reason': 'checkpoint training/model-selection lineage is incomplete'}
        if status == 'seen':
            return {'verified': False, 'reason': 'query design participated in gradients, model selection or an ancestor checkpoint'}
        return {'verified': True, 'reason': '', 'seen_designs': sorted(set(provenance['seen_designs']))}

    def select(self, key, mode, shots, minimum, fallback, chosen=()):
        if not 1 <= minimum <= shots <= 16 or fallback not in ('nearest', 'skip'):
            raise ValueError('invalid evaluation shots/min_shots/fallback')
        query = self.compatible[key]
        if mode == 'chosen':
            if len(chosen) < minimum or len(chosen) > shots or len(set(chosen)) != len(chosen) or key in chosen:
                raise ValueError('chosen support must contain distinct non-query IDs, within shots')
            if any(s not in self.compatible for s in chosen):
                raise ValueError('chosen support contains an unavailable/incompatible routed sample')
            return list(chosen), 'chosen', ''
        if chosen or mode not in ('earlier', 'zero-shot'):
            raise ValueError('explicit support IDs require mode=chosen')
        candidates = [r for r in self.compatible.values() if r['id'] != key and r['created_at'] < query['created_at']
                      and (mode != 'zero-shot' or r['top'] != query['top'])]
        if fallback == 'skip' and len(candidates) < shots:
            return [], 'skip', 'requested shots exceed the earlier eligible pool'
        matching = [r for r in candidates if r['top'] == query['top'] and r['technology'] == query['technology']]

        def distance(record):
            a, b = query['floorplan'], record['floorplan']
            return (abs(a['die_width'] - b['die_width']) / max(a['die_width'], 1e-20)
                    + abs(a['die_height'] - b['die_height']) / max(a['die_height'], 1e-20)
                    + abs(a['utilization'] - b['utilization']) + (a['macro_signature'] != b['macro_signature']))

        if mode != 'zero-shot' and len(matching) >= minimum:
            matching.sort(key=lambda r: (-r['created_at'], distance(r), r['id']))
            return [r['id'] for r in matching[:shots]], 'same-design', ''
        if mode != 'zero-shot' and fallback == 'skip':
            return [], 'skip', 'insufficient earlier same-design support samples'
        if len(candidates) < minimum:
            return [], 'skip', 'insufficient earlier compatible support samples'
        features = self.load(key)[0]

        def nearest(record):
            candidate = self.load(record['id'])[0]
            # Match native selection's binary64 accumulation order.
            value = 0.
            for a, b in zip(features.values, candidate.values):
                delta = a - b
                value += delta * delta
            return record['technology'] != query['technology'], value, record['id']

        candidates.sort(key=nearest)
        return [r['id'] for r in candidates[:shots]], 'nearest-other-design' if mode == 'zero-shot' else 'nearest', ''


def metric_average(values):
    result = {}
    for name in METRICS:
        mean, defined, undefined = defined_mean([r[name] for r in values])
        result[name] = {'mean': mean, 'defined': defined, 'undefined': undefined}
    return result


def averages(records):
    """Macro average defined metrics; retain each denominator and missing count."""
    return {metric: metric_average([r['metrics'][metric] for r in records if r['status'] == 'scored'])
            for metric in ('utilization', 'drc')}


def baseline_averages(records):
    return {model: {variant: metric_average([r['baseline']['models'][model][variant] for r in records
                                             if r['status'] == 'scored' and 'baseline' in r])
                    for variant in ('base', 'enhanced')} for model in ('rudy', 'rsmt')}


def evaluate(args):
    args.output = Path(args.output).resolve()
    if not 1 <= args.min_shots <= args.shots <= 16 or args.fallback not in ('nearest', 'skip'):
        raise ValueError('invalid evaluation shots/min_shots/fallback')
    pool = Pool(args.pool)
    selected = args.query or sorted(pool.records)
    if len(set(selected)) != len(selected) or any(key not in pool.records for key in selected):
        raise ValueError('queries must be distinct pool IDs')
    if args.query_top:
        selected = [key for key in selected if pool.records[key]['top'] == args.query_top]
    if not selected:
        raise ValueError('no evaluation queries match')
    if args.require_unseen_design:
        for key in selected:
            identity = pool.unseen(pool.records[key]['top'])
            if not identity['verified']:
                raise ValueError(f'unseen-design evaluation rejected for {key}: {identity["reason"]}')
    args.output.mkdir(parents=True, exist_ok=True)
    runs = args.output / 'runs'
    runs.mkdir(exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run_', dir=runs))
    snapshot = run / 'manifest_snapshot.json'
    _atomic(snapshot, canonical(pool.manifest))
    report = {'evaluation_version': 1, **VERSIONS, 'mode': args.mode, 'shots': args.shots, 'min_shots': args.min_shots,
              'fallback': args.fallback, 'requires_unseen_design': args.require_unseen_design,
              'manifest_snapshot_sha256': pool.manifest_sha256, 'weights_sha256': pool.weights_sha256,
              'manifest_snapshot': str(snapshot.relative_to(args.output)),
              'run_directory': str(run.relative_to(args.output)),
              'native_binary_sha256': sha(args.native.resolve().read_bytes()),
              'trained_on': pool.manifest.get('trained_on'), 'provenance': pool.manifest.get('provenance'), 'queries': []}
    for index, key in enumerate(selected):
        record = pool.records[key]
        result = {'id': key, 'top': record['top'], 'unseen_design': pool.unseen(record['top']),
                  'status': 'skipped', 'support_ids': []}
        report['queries'].append(result)
        if key not in pool.compatible:
            result['reason'] = 'query is unrouted or has incompatible versions'
            continue
        start = time.monotonic()
        support, selection, reason = pool.select(key, args.mode, args.shots, args.min_shots, args.fallback, args.support)
        result.update(support_ids=support, selection=selection, selection_seconds=time.monotonic() - start)
        if reason:
            result['reason'] = reason
            continue
        features, labels, paths = pool.load(key)
        directory = run / f'query_{index:06d}'
        directory.mkdir(exist_ok=True)
        prediction_path = directory / 'prediction.bin'
        command = [str(args.native.resolve()), 'predict', str(pool.weights), str(paths[0]), str(prediction_path.resolve())]
        for support_id in support:
            command.extend(str(path) for path in pool.load(support_id)[2])
        start = time.monotonic()
        subprocess.run(command, check=True, timeout=args.timeout)
        result['native_seconds'] = time.monotonic() - start
        prediction = read(prediction_path)
        if (prediction.channels, prediction.bins_x, prediction.bins_y, prediction.normalized) != (2, 256, 256, False):
            raise ValueError('native evaluation prediction shape mismatch')
        result['metrics'] = {name: metrics(absolute_channel(labels, metric), absolute_channel(prediction, metric), 256, 256)
                             for metric, name in enumerate(('utilization', 'drc'))}
        result.update(status='scored', reason='', prediction=str(prediction_path.relative_to(args.output)),
                      prediction_sha256=sha(prediction_path.read_bytes()),
                      features_sha256=record['features_sha256'], labels_sha256=record['labels_sha256'])
        if args.rudy_metrics:
            from .baseline import baseline
            result['baseline'] = baseline(inside(pool.root, record['directory']), directory / 'baseline', args.rudy_metrics)
        _atomic(directory / 'result.json', canonical(result))
    report['coverage'] = {'requested': len(selected), 'scored': sum(r['status'] == 'scored' for r in report['queries']),
                          'skipped': sum(r['status'] != 'scored' for r in report['queries'])}
    report['macro_average_over_queries'] = averages(report['queries'])
    report['by_support_selection'] = {name: {'scored': sum(r['status'] == 'scored' and r.get('selection') == name for r in report['queries']),
                                             'metrics': averages([r for r in report['queries'] if r.get('selection') == name])}
                                       for name in sorted({r['selection'] for r in report['queries'] if r['status'] == 'scored'})}
    report['baseline_macro_average_over_queries'] = baseline_averages(report['queries'])
    report['baseline_by_design'] = {top: baseline_averages([r for r in report['queries'] if r['top'] == top])
                                     for top in sorted({r['top'] for r in report['queries']})}
    report['by_design'] = {top: averages([r for r in report['queries'] if r['top'] == top])
                           for top in sorted({r['top'] for r in report['queries']})}
    _atomic(run / 'evaluation.json', canonical(report))
    _atomic(args.output / 'evaluation.json', canonical(report))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--native', type=Path, default=Path('bazel-bin/tools/uroute/native'))
    parser.add_argument('--query', action='append', default=[])
    parser.add_argument('--query-top')
    parser.add_argument('--mode', choices=('earlier', 'zero-shot', 'chosen'), default='earlier')
    parser.add_argument('--support', action='append', default=[])
    parser.add_argument('--shots', type=int, default=5)
    parser.add_argument('--min-shots', type=int, default=1)
    parser.add_argument('--fallback', choices=('nearest', 'skip'), default='nearest')
    parser.add_argument('--require-unseen-design', action='store_true')
    parser.add_argument('--rudy-metrics', type=Path, help='also compute native bbox/RSMT reference scores')
    parser.add_argument('--timeout', type=float, default=120, help='per-query native prediction deadline in seconds')
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('timeout must be finite and positive')
    result = evaluate(args)
    print(json.dumps(result['coverage'], indent=2))


if __name__ == '__main__':
    main()
