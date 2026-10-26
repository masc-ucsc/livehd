# SPDX-License-Identifier: BSD-3-Clause
"""Fold coverage, lineage admission, immutable snapshots and aggregation."""
import argparse
import json
from pathlib import Path
import tempfile
import unittest

from tools.uroute.cross_validate import clone_pool, configuration, cross_validate, design_averages, readiness
from tools.uroute.features import Maps
from tools.uroute.pool import canonical, publish_sample, read_manifest


class CrossValidationTest(unittest.TestCase):
    def fixtures(self, root):
        area = 256 * 256
        features = Maps(4, 256, 256, True, [0.] * (4 * area), [(0., 0.)] * 4)
        labels = Maps(3, 256, 256, False, [1.] * area + [0.] * area + [1.] * area,
                      [(1., 1.), (0., 0.), (1., 1.)])
        for version in range(2):
            for index, top in enumerate(('A', 'B', 'C')):
                publish_sample(root, {'top': top, 'technology': 'T', 'created_at': version * 3 + index,
                                      'source': 'external', 'status': 'routed', 'seed': version,
                                      'floorplan': {'die_width': 16., 'die_height': 16., 'utilization': .5, 'macro_signature': ''}},
                               features, labels, extra_files={'source.txt': b'synthetic test fixture only\n'})
        config = {'folds': [{'test_top': top, 'validation_task': [other, 'T', 0]}
                            for top, other in [('A', 'B'), ('B', 'C'), ('C', 'A')]]}
        return read_manifest(root), config

    def test_readiness_and_source_isolation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            manifest, config = self.fixtures(source)
            original = (source / 'manifest.json').read_bytes()
            with self.assertRaisesRegex(ValueError, 'outside'):
                clone_pool(source, source / 'nested-fold', manifest, True)
            audit = readiness(source, manifest, config, True)
            self.assertTrue(audit['ready'], audit)
            self.assertFalse(audit['data_bytes_verified'])
            for fold in audit['folds']:
                self.assertNotIn(fold['test_top'], fold['gradient_designs'])
                self.assertGreater(fold['queries_with_earlier_same_task'], 0)
                self.assertGreater(fold['queries_with_earlier_other_design'], 0)
            clone = root / 'fold'
            clone_pool(source, clone, manifest, True)
            self.assertEqual(read_manifest(clone)['samples'], manifest['samples'])
            (clone / 'manifest.json').write_bytes(canonical(dict(read_manifest(clone), trained_on='isolated change')))
            self.assertEqual(original, (source / 'manifest.json').read_bytes())
            config_path = root / 'folds.json'
            config_path.write_bytes(canonical(config))
            result = cross_validate(argparse.Namespace(pool=source, config=config_path, output=root / 'audit',
                                                       from_scratch=True, check_only=True))
            self.assertEqual(result['status'], 'ready')
            self.assertFalse(list((root / 'audit').rglob('weights*.bin')))
            self.assertEqual(original, (source / 'manifest.json').read_bytes())
            corrupted = source / manifest['samples'][0]['directory'] / 'source.txt'
            corrupted.write_text('changed')
            with self.assertRaisesRegex(ValueError, 'modified'):
                clone_pool(source, root / 'bad-fold', manifest, True)

    def test_missing_coverage_and_contaminated_lineage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, config = self.fixtures(root / 'pool')
            missing = dict(manifest, samples=[r for r in manifest['samples'] if r['top'] != 'C'])
            audit = readiness(root / 'pool', missing, config, True)
            self.assertFalse(audit['ready'])
            self.assertTrue(all(f['errors'] for f in audit['folds']))
            contaminated = dict(manifest, weights={'file': 'not-opened.bin', 'sha256': 'a' * 64},
                                provenance={'lineage_complete': True, 'seen_designs': ['A', 'B', 'C']})
            audit = readiness(root / 'pool', contaminated, config, False)
            self.assertFalse(audit['ready'])
            self.assertTrue(all(any('already seen' in e for e in f['errors']) for f in audit['folds']))
            path = root / 'invalid.json'
            path.write_text(json.dumps({'folds': [config['folds'][0], config['folds'][0]]}))
            with self.assertRaisesRegex(ValueError, 'distinct'):
                configuration(path)

    def test_two_arms_with_different_technologies_are_separate_tasks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, config = self.fixtures(root / 'pool')
            # Two observations of B are insufficient when each uses a
            # different technology: neither task has a distinct support.
            records = [dict(r) for r in manifest['samples']]
            next(r for r in records if r['top'] == 'B')['technology'] = 'other-LEFs'
            audit = readiness(root / 'pool', dict(manifest, samples=records), config, True)
            self.assertFalse(audit['ready'])
            coverage = [t for t in audit['task_coverage'] if t['top'] == 'B']
            self.assertEqual({t['technology'] for t in coverage}, {'T', 'other-LEFs'})
            self.assertTrue(all(t['routed_placements'] == 1 and t['additional_placements_required'] == 1
                                and t['queries_with_earlier_support'] == 0 for t in coverage))
            by_top = {fold['test_top']: fold for fold in audit['folds']}
            self.assertEqual(by_top['A']['incomplete_training_tasks'], coverage)
            self.assertEqual(by_top['B']['incomplete_training_tasks'], [])
            self.assertTrue(by_top['B']['errors'])

    def test_design_average_preserves_undefined_counts(self):
        folds = []
        for top, value in [('A', .2), ('B', .8), ('C', None)]:
            metric = {k: {'mean': value, 'defined': int(value is not None), 'undefined': int(value is None)}
                      for k in ('pearson', 'spearman', 'ssim', 'nrms', 'rmse', 'hotspot_top5_overlap')}
            evaluation = {'by_design': {top: {'utilization': metric, 'drc': metric}}}
            folds.append({'test_top': top, 'evaluations': {'earlier': evaluation, 'zero-shot': evaluation}})
        result = design_averages(folds)['earlier']['utilization']['pearson']
        self.assertEqual(result, {'mean': .5, 'defined_designs': 2, 'undefined_designs': 1})


if __name__ == '__main__':
    unittest.main()
