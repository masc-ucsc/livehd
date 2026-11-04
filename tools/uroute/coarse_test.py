# SPDX-License-Identifier: BSD-3-Clause
"""Coarse labels cannot become fabricated DRC or silently mix label sources."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.uroute.calibrate import calibrate
from tools.uroute.coarse import eligible_metrics, ingest, validate_metadata
from tools.uroute.export_weights import encode_tensors
from tools.uroute.features import Maps
from tools.uroute.fixture import tensors
from tools.uroute.maps import encode, raw, read
from tools.uroute.pool import canonical, load_sample, publish_sample, publish_weights, sha
from tools.uroute.train import Dataset

BINARY = str(Path(sys.argv.pop(1)).resolve())
N = 256 * 256


class CoarseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.weights = encode_tensors(tensors())

    def exported(self, path):
        subprocess.run([BINARY, 'export-coarse', str(path)], check=True, timeout=10)
        return json.loads((path / 'metadata.json').read_text())

    def select(self, export, pool, policy='detailed', drc=None):
        command = [BINARY, str(export / 'features.bin'), str(pool), 'fixture', 'tech', '2', '1', 'nearest', 'macro', policy]
        if drc is not None:
            command.append(str(drc))
        return json.loads(subprocess.run(command, check=True, capture_output=True, text=True, timeout=10).stdout)

    def test_export_ingestion_task_mask_and_default_isolation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); export = root / 'export'; pool = root / 'pool'
            metadata = self.exported(export)
            self.assertEqual(metadata['directional_definition'], 'incident_edge_max_then_overlapping_tile_max')
            maps = read(export / 'directional_labels.bin')
            self.assertEqual(maps.channels, 2)
            self.assertEqual(maps.values[:N], [.5] * N)
            self.assertEqual(maps.values[N:], [.25] * N)
            record = ingest(export, pool)
            self.assertEqual(ingest(export, pool)['id'], record['id'])
            self.assertFalse(record['physically_routed'])
            self.assertEqual(record['eligible_tasks'], ['utilization'])
            _, labels, _ = load_sample(pool, record)
            self.assertEqual(labels.channels, 1)
            publish_weights(pool, self.weights, trained_on='synthetic-test-only', provenance={'source': 'synthetic-test-only'})
            self.assertFalse(self.select(export, pool)['usable'])
            selected = self.select(export, pool, 'coarse-only')
            self.assertTrue(selected['usable'], selected)
            self.assertFalse(selected['drc_available'])
            self.assertEqual(selected['utilization_first'], .5)
            self.assertIn('DRC feedback requires', self.select(export, pool, 'coarse-only', 1)['skipped'])
            self.assertFalse(self.select(export, pool, 'calibrated-mixed')['usable'])
            self.assertEqual(eligible_metrics(record), ())
            self.assertEqual(eligible_metrics(record, 'coarse-only'), (0,))
            # An unresolved observation is retained, but never becomes support.
            metadata.update(complete=False, unrouted=1, eligible_tasks=[])
            (export / 'metadata.json').write_bytes(canonical(metadata))
            failed = ingest(export, pool)
            self.assertEqual(eligible_metrics(failed, 'coarse-only'), ())
            self.assertEqual(self.select(export, pool, 'coarse-only')['sample_ids'], [record['id']])
            bad = dict(metadata, complete=True)
            with self.assertRaisesRegex(ValueError, 'unresolved'):
                validate_metadata(bad)
            bad = dict(metadata, map_semantics='penalty_augmented')
            with self.assertRaisesRegex(ValueError, 'raw'):
                validate_metadata(bad)
            # Tampering cannot be hidden by a stale hash.
            (export / 'directional_labels.bin').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'digest'):
                ingest(export, pool)

    def paired(self, root, name, scale=(2, 3), source='openroad_detailed'):
        export = root / (name + '_coarse'); detailed = root / (name + '_reference'); detailed.mkdir()
        metadata = self.exported(export)
        metadata.update(top=name, pose_sha256=sha(name.encode()), netlist_sha256=sha((name + 'nets').encode()))
        (export / 'metadata.json').write_bytes(canonical(metadata))
        groups = raw([[scale[d] * (.5 if d == 0 else .25)] * N for _ in range(3) for d in range(2)], 256, 256)
        reference = {k: metadata[k] for k in ('top', 'technology', 'core', 'dbu_per_micron', 'pose_sha256', 'netlist_sha256')}
        reference.update(status='routed', label_source=source, capacity_sha256='d' * 64,
                         directional_definition='max_group_centerline_utilization_M2M3_M4M5_M6plus')
        if source == 'openroad_global':
            groups = raw([[scale[0] * .5] * N, [scale[1] * .25] * N], 256, 256)
            reference.update(status='global', complete=True, directional_definition='incident_edge_max_then_overlapping_tile_max')
            data = encode(groups); (detailed / 'directional_labels.bin').write_bytes(data)
            reference['directional_labels_sha256'] = sha(data)
        else:
            data = encode(groups); (detailed / 'layer_groups.bin').write_bytes(data)
            reference['files'] = {'layer_groups.bin': sha(data)}
        (detailed / 'sample.json').write_bytes(canonical(reference))
        return {'coarse': str(export), 'detailed': str(detailed)}

    def test_paired_calibration_and_heldout_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pairs = [self.paired(root, name) for name in ('fit', 'held')]
            report = calibrate(pairs, ['held'], .01)
            self.assertEqual(report['scale_hv'], [2., 3.])
            self.assertTrue(report['accepted'])
            calibration = root / 'calibration.json'; calibration.write_bytes(canonical(report))
            pool = root / 'pool'; export = Path(pairs[1]['coarse'])
            record = ingest(export, pool, calibration)
            self.assertEqual(load_sample(pool, record)[1].values, [1.] * N)
            publish_weights(pool, self.weights, trained_on='synthetic-test-only', provenance={'source': 'synthetic-test-only'})
            result = self.select(export, pool, 'calibrated-mixed')
            self.assertTrue(result['usable'], result)
            self.assertFalse(result['drc_available'])
            self.assertEqual(result['utilization_first'], 1.)
            self.assertFalse(self.select(export, pool, 'coarse-only')['usable'])
            with self.assertRaisesRegex(ValueError, 'fit and all held-out'):
                calibrate(pairs, ['fit', 'held'], .01)
            # Holdout cannot modify fitted coefficients; only acceptance changes.
            badpair = self.paired(root, 'badheld', scale=(20, 30))
            bad = calibrate([pairs[0], badpair], ['badheld'], .01)
            self.assertEqual(bad['scale_hv'], [2., 3.]); self.assertFalse(bad['accepted'])
            calibration.write_bytes(canonical(bad))
            with self.assertRaisesRegex(ValueError, 'acceptance'):
                ingest(export, root / 'badpool', calibration)
            path = Path(pairs[1]['detailed']) / 'sample.json'
            reference = json.loads(path.read_text()); reference['pose_sha256'] = '0' * 64; path.write_bytes(canonical(reference))
            with self.assertRaisesRegex(ValueError, 'pose_sha256'):
                calibrate(pairs, ['held'], .01)

    def test_global_reference_is_diagnostic_not_detailed_authorization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pairs = [self.paired(root, name, source='openroad_global') for name in ('fit', 'held')]
            report = calibrate(pairs, ['held'], .01)
            self.assertTrue(report['accepted'])
            calibration = root / 'calibration.json'; calibration.write_bytes(canonical(report))
            with self.assertRaisesRegex(ValueError, 'does not authorize'):
                ingest(pairs[0]['coarse'], root / 'pool', calibration)

    def test_training_task_policy_utilization_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); export = root / 'export'; pool = root / 'pool'
            base = self.exported(export)
            for name in ('A', 'B', 'held'):
                for iteration in range(2):
                    metadata = dict(base, top=name, pose_sha256=sha(f'{name}{iteration}'.encode()))
                    (export / 'metadata.json').write_bytes(canonical(metadata)); ingest(export, pool)
            with self.assertRaisesRegex(ValueError, 'no compatible'):
                Dataset(pool, 'held')
            dataset = Dataset(pool, 'held', label_policy='coarse-only')
            self.assertEqual(len(dataset.train), 2)
            self.assertTrue(all(task[2] == 0 for task in (*dataset.train, *dataset.validation)))


if __name__ == '__main__':
    unittest.main()
