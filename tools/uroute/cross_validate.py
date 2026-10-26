# SPDX-License-Identifier: BSD-3-Clause
"""Serial, isolated leave-one-design-out training and native evaluation.

Every fold explicitly names a test design and a different validation task.
The input pool is read-only. Readiness checks never train or create weights.
"""
import argparse
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from .pool import (VERSIONS, _atomic, _identity, _object, _verify_immutable_sample,
                   canonical, file_sha, read_manifest, sha)
from .train import Dataset


def configuration(path):
    value = json.loads(Path(path).read_bytes(), object_pairs_hook=_object)
    if not isinstance(value, dict) or set(value) != {'folds'} or not isinstance(value['folds'], list) or not value['folds']:
        raise ValueError('fold configuration must contain a nonempty folds list')
    seen = set()
    for fold in value['folds']:
        if not isinstance(fold, dict) or set(fold) != {'test_top', 'validation_task'}:
            raise ValueError('each fold requires test_top and validation_task')
        top, task = fold['test_top'], fold['validation_task']
        if not isinstance(top, str) or not top or '\0' in top or top in seen:
            raise ValueError('test designs must be distinct nonempty names')
        seen.add(top)
        if (not isinstance(task, list) or len(task) != 3 or
                not all(isinstance(v, str) and v and '\0' not in v for v in task[:2]) or
                type(task[2]) is not int or task[2] not in (0, 1) or task[0] == top):
            raise ValueError('validation_task must be [other_top,technology,0|1]')
    return value


def readiness(root, manifest, config, from_scratch):
    """Metadata-only coverage audit against one frozen manifest snapshot."""
    records = [r for r in manifest['samples'] if r.get('status') == 'routed' and
               all(type(r.get(k)) is int and r[k] == v for k, v in VERSIONS.items())]
    ids = [r.get('id') for r in records]
    if any(not isinstance(i, str) or not i for i in ids) or len(set(ids)) != len(ids):
        raise ValueError('routed sample IDs must be distinct nonempty strings')
    for record in records:
        _identity(record)
    # A design's two arms can have different LEF fingerprints. They then
    # belong to separate tasks and cannot support one another during training.
    tasks = defaultdict(list)
    for record in records:
        tasks[(record['top'], record['technology'])].append(record)
    task_coverage = [
        {'top': top, 'technology': technology, 'routed_placements': len(samples),
         'additional_placements_required': max(0, 2 - len(samples)),
         'queries_with_earlier_support': sum(any(s['created_at'] < q['created_at'] for s in samples)
                                           for q in samples)}
        for (top, technology), samples in sorted(tasks.items())]
    folds = []
    with tempfile.TemporaryDirectory(prefix='uroute_fold_preflight_') as temporary:
        frozen = Path(temporary)
        (frozen / 'manifest.json').write_bytes(canonical(manifest))
        for fold in config['folds']:
            top, task = fold['test_top'], tuple(fold['validation_task'])
            queries = [r for r in records if r['top'] == top]
            result = dict(fold, routed_queries=len(queries), errors=[])
            result['incomplete_training_tasks'] = [t for t in task_coverage
                                                   if t['top'] != top and t['additional_placements_required']]
            if len(queries) < 2:
                result['errors'].append('test design requires at least two routed query/support placements')
            result['queries_with_earlier_same_task'] = sum(any(
                s['top'] == top and s['technology'] == q['technology'] and s['created_at'] < q['created_at']
                for s in records) for q in queries)
            result['queries_with_earlier_other_design'] = sum(any(
                s['top'] != top and s['created_at'] < q['created_at'] for s in records) for q in queries)
            if not result['queries_with_earlier_same_task']:
                result['errors'].append('no query has an earlier same-top/technology support')
            if not result['queries_with_earlier_other_design']:
                result['errors'].append('no query has an earlier other-design zero-shot support')
            try:
                data = Dataset(frozen, None, [top], task)
                result['gradient_designs'] = sorted({t[0] for t in data.train})
                result['gradient_tasks'] = [list(t) for t in sorted(data.train)]
                result['validation_tasks'] = [list(t) for t in sorted(data.validation)]
            except ValueError as error:
                result['errors'].append(str(error))
            if not from_scratch:
                provenance = manifest.get('provenance', {})
                seen = provenance.get('seen_designs') if isinstance(provenance, dict) else None
                if not manifest.get('weights'):
                    result['errors'].append('no initial weights; explicitly choose --from-scratch')
                elif (not isinstance(provenance, dict) or provenance.get('lineage_complete') is not True or
                      not isinstance(seen, list) or not seen or not all(isinstance(s, str) and s for s in seen)):
                    result['errors'].append('initial checkpoint has incomplete training/model-selection lineage')
                elif top in seen:
                    result['errors'].append('initial checkpoint has already seen the test design')
            folds.append(result)
    return {'ready': all(not f['errors'] for f in folds), 'data_bytes_verified': False,
            'task_coverage': task_coverage,
            'routed_samples': len(records), 'folds': folds,
            'note': 'metadata coverage only; immutable file hashes are verified before any fold training'}


def clone_pool(source, destination, manifest, from_scratch):
    """Snapshot immutable files; hard links avoid multiplying large geometry."""
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if destination.is_relative_to(source):
        raise ValueError('fold snapshot must be outside the read-only source pool')
    destination.mkdir(parents=True, exist_ok=False)

    def install(original, relative):
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('fold snapshot path escapes its directory')
        target = destination / path
        if target.exists():
            raise ValueError('fold snapshot file collision')
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(original, target)
        except OSError:
            shutil.copyfile(original, target)
            if file_sha(original) != file_sha(target):
                raise ValueError('fold snapshot changed while copying')

    snapshot = dict(manifest)
    # Observations are supplementary evidence, not support shots. Keep the
    # source manifest alongside the run, while copying only its sample objects.
    snapshot.pop('observations', None)
    for record in manifest['samples']:
        directory = _verify_immutable_sample(source, record)
        for name in ['sample.json', *record['files']]:
            install(directory / name, Path(record['directory']) / name)
    if from_scratch:
        snapshot.update(weights=None, trained_on='', provenance={})
    elif manifest.get('weights'):
        weights = manifest['weights']
        relative = Path(weights['file'])
        original = (source / relative).resolve()
        if relative.is_absolute() or not original.is_relative_to(source) or original == source:
            raise ValueError('initial weight file escapes its source pool')
        if original.stat().st_size > 256 * 1024 * 1024 or file_sha(original) != weights['sha256']:
            raise ValueError('initial weight digest mismatch')
        install(original, relative)
    _atomic(destination / 'manifest.json', canonical(snapshot))


def design_averages(folds):
    """Average design means, preserving undefined-design denominators."""
    result = {}
    for mode in ('earlier', 'zero-shot'):
        result[mode] = {}
        for metric in ('utilization', 'drc'):
            result[mode][metric] = {}
            for name in ('pearson', 'spearman', 'ssim', 'nrms', 'rmse', 'hotspot_top5_overlap'):
                values = [f['evaluations'][mode]['by_design'][f['test_top']][metric][name]['mean'] for f in folds]
                defined = [v for v in values if v is not None]
                result[mode][metric][name] = {'mean': math.fsum(defined) / len(defined) if defined else None,
                                             'defined_designs': len(defined), 'undefined_designs': len(values) - len(defined)}
    return result


def cross_validate(args):
    root, output = args.pool.resolve(), args.output.resolve()
    if output.is_relative_to(root):
        raise ValueError('cross-validation output must be outside the read-only source pool')
    config, manifest = configuration(args.config), read_manifest(root)
    audit = readiness(root, manifest, config, args.from_scratch)
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run_', dir=output))
    report = {'cross_validation_version': 1, **VERSIONS, 'pid': os.getpid(), 'started_at': time.time_ns(),
              'run_directory': str(run), 'source_manifest_sha256': sha(canonical(manifest)),
              'config': config, 'readiness': audit, 'folds': [], 'status': 'checked'}
    _atomic(run / 'source_manifest.json', canonical(manifest))

    def save():
        _atomic(run / 'cross_validation.json', canonical(report))
        _atomic(output / 'cross_validation.json', canonical(report))

    save()
    if args.check_only or not audit['ready']:
        report['status'] = 'ready' if audit['ready'] else 'insufficient-coverage'
        save()
        return report
    try:
        from .evaluate import evaluate
        from .train import train
        report['status'] = 'running'
        for index, fold in enumerate(config['folds']):
            directory = run / f'fold_{index:03d}'
            pool = directory / 'pool'
            clone_pool(root, pool, manifest, args.from_scratch)
            result = dict(fold, directory=str(directory), status='training', evaluations={})
            report['folds'].append(result)
            save()
            training = argparse.Namespace(pool=pool, holdout=None, validation_task=tuple(fold['validation_task']),
                                          exclude_design=[fold['test_top']], from_scratch=args.from_scratch,
                                          shots=args.shots, epochs=args.epochs, episodes=args.episodes, patience=args.patience,
                                          learning_rate=args.learning_rate, seed=args.seed, threads=args.threads, device=args.device)
            training.sampling = getattr(args, 'sampling', 'sqrt-mean-floor')
            training.linear_head_loss = getattr(args, 'linear_head_loss', False)
            training.mps_memory_limit_gib = getattr(args, 'mps_memory_limit_gib', None)
            result['training'] = train(training)
            for mode in ('earlier', 'zero-shot'):
                result['status'] = 'evaluating-' + mode
                save()
                options = argparse.Namespace(pool=pool, output=directory / mode, native=args.native,
                                             query=[], query_top=fold['test_top'], mode=mode, support=[],
                                             shots=args.shots, min_shots=1, fallback='nearest', require_unseen_design=True,
                                             rudy_metrics=args.rudy_metrics, timeout=args.timeout)
                result['evaluations'][mode] = evaluate(options)
            result['status'] = 'evaluated'
            save()
        report['design_macro_average'] = design_averages(report['folds'])
        report['has_scores_for_every_fold_protocol'] = all(
            f['evaluations'][m]['coverage']['scored'] > 0 for f in report['folds'] for m in ('earlier', 'zero-shot'))
        report['all_requested_queries_scored'] = all(
            f['evaluations'][m]['coverage']['skipped'] == 0 for f in report['folds'] for m in ('earlier', 'zero-shot'))
        report['status'] = 'evaluated'
    except Exception as error:
        report['status'], report['error'] = 'failed', str(error)
        save()
        raise
    save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pool', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True, help='JSON folds: test_top and [top,technology,metric] validation_task')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--from-scratch', action='store_true')
    parser.add_argument('--native', type=Path, default=Path('bazel-bin/tools/uroute/native'))
    parser.add_argument('--rudy-metrics', type=Path)
    parser.add_argument('--shots', type=int, default=5)
    parser.add_argument('--sampling', choices=('sqrt-mean-floor', 'paper-rms'), default='sqrt-mean-floor')
    parser.add_argument('--linear-head-loss', action='store_true')
    parser.add_argument('--epochs', type=int, default=100)
    parser.add_argument('--episodes', type=int, default=0)
    parser.add_argument('--patience', type=int, default=10)
    parser.add_argument('--learning-rate', type=float, default=.0003)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument('--device', choices=('auto', 'cpu', 'cuda', 'mps'), default='auto')
    parser.add_argument('--mps-memory-limit-gib', type=float,
                        help='required positive Metal allocator cap for --device mps')
    parser.add_argument('--timeout', type=float, default=120)
    args = parser.parse_args()
    if not 1 <= args.shots <= 16 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('invalid shots or prediction timeout')
    try:
        report = cross_validate(args)
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(2, f'uroute cross-validation: {error}\n')
    print(json.dumps({'status': report['status'], 'run_directory': report['run_directory'], 'readiness': report['readiness']}, indent=2))
    if not report['readiness']['ready']:
        parser.exit(2)


if __name__ == '__main__':
    main()
