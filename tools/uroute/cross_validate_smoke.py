# SPDX-License-Identifier: BSD-3-Clause
"""Offline structural smoke for serial folds; synthetic data/weights are deleted.

Uses constant labels to keep native calibration analytic. This tests isolation,
exclusion and orchestration, not learned prediction quality or native arithmetic.
"""
import argparse
from pathlib import Path
import tempfile
import time

from .cross_validate import cross_validate
from .features import Maps
from .pool import canonical, file_sha, publish_sample, read_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    args = parser.parse_args()
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='uroute_cross_validate_smoke_') as temporary:
        root = Path(temporary)
        pool = root / 'source'
        area = 256 * 256
        labels = Maps(3, 256, 256, False, [1.] * area + [0.] * area + [1.] * area,
                      [(1., 1.), (0., 0.), (1., 1.)])
        for version in range(2):
            for index, top in enumerate(('A', 'B', 'C')):
                value = .1 * (1 + version + index)
                features = Maps(4, 256, 256, True, [value] * (4 * area), [(0., 1.)] * 4)
                publish_sample(pool, {'top': top, 'technology': 'synthetic-only', 'created_at': version * 3 + index,
                                      'source': 'external', 'status': 'routed', 'seed': version,
                                      'floorplan': {'die_width': 16., 'die_height': 16., 'utilization': .5, 'macro_signature': ''}},
                               features, labels)
        config = root / 'folds.json'
        config.write_bytes(canonical({'folds': [{'test_top': top, 'validation_task': [other, 'synthetic-only', 0]}
                                                for top, other in [('A', 'B'), ('B', 'C'), ('C', 'A')]]}))
        before = file_sha(pool / 'manifest.json')
        options = argparse.Namespace(pool=pool, output=root / 'evaluation', config=config, from_scratch=True,
                                     check_only=False, native=args.native, rudy_metrics=None, timeout=20,
                                     shots=1, epochs=1, episodes=1, patience=1, learning_rate=.0003,
                                     seed=1, threads=1, device='cpu')
        report = cross_validate(options)
        assert report['status'] == 'evaluated' and len(report['folds']) == 3
        assert report['has_scores_for_every_fold_protocol']
        # The earliest query has no historical support. This must remain a
        # reported skip instead of silently taking a later sample.
        assert not report['all_requested_queries_scored']
        for fold in report['folds']:
            provenance = fold['training']['provenance']
            assert fold['test_top'] not in provenance['seen_designs']
            assert provenance['excluded_designs'] == [fold['test_top']]
            assert provenance['initial_weights_sha256'] is None
            for evaluation in fold['evaluations'].values():
                assert evaluation['requires_unseen_design']
                assert all(q['unseen_design']['verified'] for q in evaluation['queries'])
        means = report['design_macro_average']['earlier']['drc']['pearson']
        assert means['mean'] is None and means['undefined_designs'] == 3
        assert file_sha(pool / 'manifest.json') == before and read_manifest(pool)['weights'] is None
        print(canonical({'status': 'passed', 'folds': 3, 'source_pool_unchanged': True,
                         'scope': 'synthetic structural smoke; constant calibration; not quality evidence',
                         'temporary_weights_deleted_at_exit': True,
                         'seconds': time.monotonic() - started}).decode(), end='')


if __name__ == '__main__':
    main()
