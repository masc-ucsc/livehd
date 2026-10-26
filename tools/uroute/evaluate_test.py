# SPDX-License-Identifier: BSD-3-Clause
"""Chronology, query exclusion, calibration, coverage and model lineage gates."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

from tools.uroute.evaluate import Pool, evaluate
from tools.uroute.export_weights import encode_tensors
from tools.uroute.features import Maps
from tools.uroute.fixture import tensors
from tools.uroute.pool import publish_sample, publish_weights
from tools.uroute.train import Dataset, parse_task, training_implementation

BINARY = Path(sys.argv.pop(1)).resolve()
SELECTOR = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
AREA = 256 * 256


def metadata(top, time, tech='T'):
    return {'top': top, 'technology': tech, 'created_at': time, 'source': 'external', 'status': 'routed',
            'floorplan': {'die_width': 10, 'die_height': 20, 'utilization': .5, 'macro_signature': ''}}


def features(value):
    return Maps(4, 256, 256, True, [value] * (4 * AREA), [(0, 1)] * 4)


def labels(value=.5):
    return Maps(3, 256, 256, False, [value] * AREA + [0.] * AREA + [1.] * AREA,
                [(value, value), (0, 0), (1, 1)])


class EvaluateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.weights = encode_tensors(tensors())

    def test_training_source_snapshot_is_immutable_without_torch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            saved = training_implementation(root / 'train')
            self.assertEqual(training_implementation(root / 'train'), saved)
            copied = root / saved['directory'] / 'train.py'
            original = copied.read_bytes()
            copied.write_bytes(b'changed retained training source')
            with self.assertRaisesRegex(ValueError, 'immutable training source snapshot changed'):
                training_implementation(root / 'train')
            copied.write_bytes(original)
            (copied.parent / 'manifest.json').write_text('{}\n')
            with self.assertRaisesRegex(ValueError, 'immutable training source manifest changed'):
                training_implementation(root / 'train')

    def test_protocol_and_native_calibration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pool_dir = root / 'pool'
            old = publish_sample(pool_dir, metadata('A', 10), features(.25), labels())
            same_time = publish_sample(pool_dir, metadata('A', 20), features(.2), labels(.2))
            query = publish_sample(pool_dir, metadata('A', 20), features(0), labels())
            future = publish_sample(pool_dir, metadata('A', 30), features(0), labels(20))
            other = publish_sample(pool_dir, metadata('B', 5), features(.1), labels())
            alien = publish_sample(pool_dir, metadata('C', 4, 'other-tech'), features(0), labels())
            failed_metadata = metadata('A', 40)
            failed_metadata['status'] = 'unrouted'
            failed = publish_sample(pool_dir, failed_metadata, features(0), last_global_overflow=Maps(1, 1, 1, False, [1.], [(1, 1)]))
            publish_weights(pool_dir, self.weights, trained_on='synthetic-test-only',
                            provenance={'source': 'synthetic-test-only', 'seen_designs': ['B', 'C'], 'lineage_complete': True})
            pool = Pool(pool_dir)
            supports = pool.select(query['id'], 'earlier', 5, 1, 'nearest')[0]
            self.assertEqual(supports, [old['id']])
            self.assertNotIn(query['id'], supports)
            self.assertNotIn(same_time['id'], supports)
            self.assertNotIn(future['id'], supports)
            self.assertEqual(pool.select(query['id'], 'zero-shot', 1, 1, 'nearest')[0], [other['id']])
            self.assertEqual(pool.select(query['id'], 'earlier', 2, 2, 'nearest')[0], [other['id'], old['id']])
            self.assertTrue(pool.select(old['id'], 'earlier', 2, 1, 'skip')[2])
            with self.assertRaises(ValueError):
                pool.select(query['id'], 'chosen', 5, 1, 'nearest', [query['id']])
            if SELECTOR:
                manifest_path = pool_dir / 'manifest.json'
                original = manifest_path.read_bytes()
                available = json.loads(original)
                available['samples'] = [r for r in available['samples'] if r['status'] == 'routed' and r['created_at'] < 20]
                try:
                    manifest_path.write_text(json.dumps(available))
                    query_path = pool.load(query['id'])[2][0]
                    for shots, minimum in ((5, 1), (2, 2)):
                        result = subprocess.run([str(SELECTOR), str(query_path), str(pool_dir), 'A', 'T',
                                                 str(shots), str(minimum), 'nearest', ''],
                                                check=True, text=True, capture_output=True, timeout=10)
                        self.assertEqual(json.loads(result.stdout)['sample_ids'],
                                         pool.select(query['id'], 'earlier', shots, minimum, 'nearest')[0])
                finally:
                    manifest_path.write_bytes(original)
            self.assertTrue(pool.unseen('A')['verified'])
            self.assertFalse(pool.unseen('B')['verified'])
            args = argparse.Namespace(pool=pool_dir, output=root / 'evaluation', query=[query['id'], failed['id']],
                                      query_top=None, require_unseen_design=True, mode='earlier', shots=1, min_shots=1,
                                      fallback='nearest', support=[], native=BINARY, timeout=10, rudy_metrics=None)
            report = evaluate(args)
            self.assertEqual(report['coverage'], {'requested': 2, 'scored': 1, 'skipped': 1})
            scored = report['queries'][0]
            self.assertEqual(scored['support_ids'], [old['id']])
            for metric in ('utilization', 'drc'):
                self.assertEqual(scored['metrics'][metric]['rmse'], 0)
                self.assertIsNone(scored['metrics'][metric]['pearson'])
                self.assertEqual(report['macro_average_over_queries'][metric]['pearson']['undefined'], 1)
            self.assertTrue((args.output / report['manifest_snapshot']).exists())
            self.assertEqual(json.loads((args.output / 'evaluation.json').read_text())['coverage'], report['coverage'])
            first_prediction = (args.output / scored['prediction']).read_bytes()
            replay = evaluate(args)
            self.assertNotEqual(replay['run_directory'], report['run_directory'])
            self.assertEqual((args.output / scored['prediction']).read_bytes(), first_prediction)
            self.assertTrue((args.output / report['run_directory'] / 'evaluation.json').exists())
            # Complete lineage includes early stopping, not merely gradients.
            publish_weights(pool_dir, self.weights, trained_on='synthetic-test-only',
                            provenance={'source': 'synthetic-test-only', 'seen_designs': ['A', 'B'], 'lineage_complete': True})
            with self.assertRaisesRegex(ValueError, 'unseen-design'):
                evaluate(args)
            publish_weights(pool_dir, self.weights, trained_on='synthetic-test-only', provenance={'source': 'unknown'})
            self.assertIn('incomplete', Pool(pool_dir).unseen('A')['reason'])

    def test_training_design_separation_without_torch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for design in ('A', 'B', 'TEST'):
                for i in range(2):
                    publish_sample(root, metadata(design, i), features(i), labels())
            dataset = Dataset(root, None, ['TEST'], ('A', 'T', 0))
            self.assertEqual(set(dataset.validation), {('A', 'T', 0)})
            self.assertEqual({task[0] for task in dataset.train}, {'A', 'B'})
            self.assertNotIn(('A', 'T', 0), dataset.train)
            self.assertTrue(all(r['top'] != 'TEST' for r in dataset.records.values()))
            with self.assertRaisesRegex(ValueError, 'excluded'):
                Dataset(root, 'TEST', ['TEST'])
            self.assertEqual(parse_task('["A","T",0]'), ('A', 'T', 0))
            for value in ('null', '["A","T",true]', '["A","T",2]'):
                with self.assertRaises(argparse.ArgumentTypeError):
                    parse_task(value)


if __name__ == '__main__':
    unittest.main()
