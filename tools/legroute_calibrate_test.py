# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Checks that diagnostic routing cannot become a class calibration label."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import legroute_calibrate as calibrate


def box_fixture():
    return {'cells': [{'name': 'a', 'y': 0, 'width_dbu': 20},
                      {'name': 'b', 'y': 100, 'width_dbu': 30}],
            'site_dbu': 10, 'free_segments': [{'row': 0, 'lo_site': 0, 'hi_site': 8}],
            'nets': [{'name': 'inside', 'clock': 0, 'power': 0, 'routed': 1,
                      'pins': [{'owner': 'a', 'in_box': 1}, {'owner': 'b', 'in_box': 1}]},
                     {'name': 'ordinary_without_route', 'routed': 0,
                      'pins': [{'owner': 'a', 'in_box': 1}, {'owner': 'outside', 'in_box': 0}]},
                     {'name': 'clk', 'clock': 1, 'routed': 0,
                      'pins': [{'owner': 'b', 'in_box': 1}, {'owner': 'outside', 'in_box': 0}]}]}


class CalibrationInventoryTest(unittest.TestCase):
    def test_fold_is_stable_across_pose_and_menu_variants(self):
        original = box_fixture()
        moved = copy.deepcopy(original)
        moved['cells'].reverse()
        moved['cells'][0]['y'] = 0
        moved['menu'] = {'M3': [13, 23]}
        self.assertEqual(calibrate.member_identity('frozen-input', original),
                         calibrate.member_identity('frozen-input', moved))
        moved['cells'][0]['name'] = 'different'
        self.assertNotEqual(calibrate.member_identity('frozen-input', original)[0],
                            calibrate.member_identity('frozen-input', moved)[0])

    def test_missing_route_is_still_in_ordinary_inventory(self):
        features = calibrate.box_features(box_fixture())
        self.assertEqual(features['internal_nets'], 1)
        self.assertEqual(features['external_nets'], 1)
        self.assertEqual(features['clock_nets'], 1)
        self.assertEqual(features['current_multirow_internal_nets'], 1)
        self.assertEqual(features['member_width_sites'], 5)
        self.assertEqual(features['unresolved_ordinary_scope_nets'], ['ordinary_without_route'])
        self.assertEqual(features['unresolved_scope_nets'], ['clk', 'ordinary_without_route'])

    def test_legacy_and_edited_success_flags_never_certify_completion(self):
        for record in ({'all_ok': True, 'signal_jobs_ok': True},
                       {'measurement_schema': 1, 'all_ok': True, 'calibration_eligible': True,
                        'unverified_obligations': [], 'certificate_status': 'ordinary_box_connected'}):
            label = calibrate.label_status(record, {'returncode': 0})
            self.assertFalse(label['eligible'])
            self.assertIn('no_supported_complete_box_certificate', label['blockers'])
        partial = calibrate.label_status({'measurement_schema': 1, 'calibration_eligible': True,
                                         'unverified_obligations': ['cell_placement_legality']}, {})
        self.assertIn('cell_placement_legality', partial['blockers'])

    def test_budget_failure_is_censored_not_exhausted(self):
        for result, process in (({'first_failure': 'maze node budget'}, {}),
                                ({'nets': [{'ok': False, 'reason': 'route deadline'}]}, {}),
                                ({}, {'timed_out': True})):
            self.assertEqual(calibrate.label_status(result, process)['outcome'], 'censored_search')
        self.assertEqual(calibrate.label_status({'first_failure': 'maze exhausted'}, {'returncode': 1})['outcome'],
                         'candidate_graph_exhausted')

    def test_diagnostic_success_needs_completed_process_and_nonempty_jobs(self):
        for record in ({'signal_jobs_ok': True}, {'signal_jobs_ok': True, 'nets': []}):
            self.assertEqual(calibrate.label_status(record, {'returncode': 0})['outcome'],
                             'no_routing_jobs')
        complete = {'signal_jobs_ok': True, 'nets': [{'ok': True}]}
        for process in (None, {}, {'elapsed_seconds': 0.1}):
            self.assertEqual(calibrate.label_status(complete, process)['outcome'],
                             'missing_process_record')
        for code in (0, 1):
            self.assertEqual(calibrate.label_status(complete, {'returncode': code})['outcome'],
                             'diagnostic_jobs_connected_or_attached')
        self.assertEqual(calibrate.label_status(complete, {'returncode': 2})['outcome'], 'process_error')
        complete['nets'] = [{'ok': False}]
        self.assertEqual(calibrate.label_status(complete, {'returncode': 0})['outcome'],
                         'diagnostic_incomplete')

    def test_only_exact_path_and_hash_can_attach_a_probe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dumps, results = root / 'dumps', root / 'results'
            dumps.mkdir()
            results.mkdir()
            box_path = dumps / 'box_c0_r0.json'
            box_path.write_text(json.dumps(box_fixture()))
            sha = calibrate.digest(box_path.read_bytes())
            wrapper = {'box': str(box_path), 'box_sha256': sha,
                       'binary_sha256': 'binary', 'config': {'menu': {'M2': [100]}},
                       'config_sha256': 'configuration', 'frame_sha256': 'frame', 'source_id': 'frozen-input',
                       'result': {'all_ok': True, 'signal_jobs_ok': True},
                       'process': {'returncode': 0, 'elapsed_seconds': 0.1, 'timed_out': False}}
            (results / 'matched.json').write_text(json.dumps(wrapper))
            (results / 'stale.json').write_text(json.dumps(dict(wrapper, box_sha256='stale')))
            (results / 'raw.json').write_text(json.dumps(wrapper['result']))
            (results / 'other_source.json').write_text(json.dumps(dict(wrapper, source_id='other-input')))
            report = calibrate.inventory('toy', 'frozen-input', dumps, results)
            self.assertEqual(len(report['boxes'][0]['measurements']), 1)
            self.assertEqual(len(report['unmatched_results']), 3)
            self.assertIn('source_id_mismatch', [item['reason'] for item in report['unmatched_results']])
            matched = report['boxes'][0]['measurements'][0]
            self.assertEqual(matched['execution_context']['binary_sha256'], 'binary')
            self.assertEqual(matched['execution_context']['config'], wrapper['config'])
            self.assertEqual(matched['execution_context']['config_sha256'], 'configuration')
            self.assertEqual(matched['execution_context']['frame_sha256'], 'frame')
            self.assertEqual(matched['execution_context']['source_id'], 'frozen-input')
            self.assertFalse(matched['execution_context']['verified'])
            self.assertIn('execution_context_unverified', matched['label']['blockers'])
            without_context = dict(wrapper)
            for key in ('binary_sha256', 'config', 'config_sha256', 'frame_sha256', 'source_id'):
                without_context.pop(key)
            without_context.pop('process')
            (results / 'no_context.json').write_text(json.dumps(without_context))
            incomplete = calibrate.inventory('toy', 'frozen-input', dumps, results)
            self.assertEqual(len(incomplete['boxes'][0]['measurements']), 2)
            missing = next(item for item in incomplete['boxes'][0]['measurements']
                           if item['path'].endswith('no_context.json'))
            self.assertEqual(missing['label']['outcome'], 'missing_process_record')
            self.assertIn('execution_context_unverified', missing['label']['blockers'])
            self.assertEqual(report['calibration']['eligible_labels'], 0)
            self.assertEqual(report['calibration']['status'], 'withheld_no_eligible_completion_labels')
            self.assertEqual(report['boxes'][0]['sha256'], sha)
            empty = calibrate.inventory('toy', 'frozen-input', dumps)
            self.assertEqual(empty['measurement_outcomes'], {})
            self.assertEqual(empty['calibration']['eligible_labels'], 0)


if __name__ == '__main__':
    unittest.main()
