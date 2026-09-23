#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import copy
import unittest

from check_witness import Budget, Exhausted, Invalid
from check_witness_test import shrinking
from summarize import network_metrics, source_metrics, summarize


def fixture():
    """One unate region (source record 0, selected decomposition record 1) and
    one ABC-fallback region with no witnesses, as pass.synth writes them."""
    record = shrinking()
    record.update(record=1, region='r', recipe_index=0, variant='recovered')
    source = {key: copy.deepcopy(record[key]) for key in ['producer_version', 'region', 'source', 'source_digest_fnv1a64']}
    source.update(schema_version=1, kind='unate_source', record=0)
    structure = network_metrics(record['network'], Budget(10000))
    attempt = {'recipe_index': 0, 'variant': 'recovered', 'witness': {'record': 1, 'status': 'archived'},
               'recipe': record['recipe'], 'status': 'feasible', 'reason': 'verified decomposition'}
    unate = {'region': 'r', 'status': 'unate', 'reason': '', 'variant': 'recovered',
             'functions': structure['function_producers'], 'twins': structure['negative_function_producers'],
             'literals': structure['literal_occurrences'], 'ports': structure['function_ports'],
             'source_inverters': structure['source_inverters'],
             'source_witness': {'record': 0, 'status': 'archived'},
             'source_metrics': source_metrics(record['source'], Budget(10000)), 'attempts': [attempt]}
    fallback = {'region': 'f', 'status': 'abc_fallback', 'reason': 'source node limit', 'variant': 'none',
                'functions': 0, 'twins': 0, 'literals': 0, 'ports': 0, 'source_inverters': 0,
                'source_witness': {'record': -1, 'status': 'not_constructed'}, 'source_metrics': None, 'attempts': []}
    report = {'schema_version': 2, 'kind': 'synth', 'witness_archive': {'records': 2},
              'totals': {'regions': 2, 'unate': 1, 'abc_fallback': 1, 'reused': 0, 'functions': unate['functions'],
                         'twins': unate['twins'], 'literals': unate['literals']},
              'regions_searched': [unate, fallback], 'regions_reused': []}
    return report, [source, record]


class Summary(unittest.TestCase):
    @staticmethod
    def invocation(report):
        return {'schema_version': 1, 'tool': 'lhd', 'command': 'synth', 'status': 'pass', 'exit_code': 0,
                'run_id': 'test-run', 'qor': {'synth': copy.deepcopy(report)},
                'phases': [{'name': 'pass.synth', 'ms': 7}],
                'synthesis_invocation': {'scope': 'main_entry_to_result_emission', 'wall_ms': 10,
                    'memory_scope': 'parent_process_peak_rss', 'parent_peak_rss_bytes': 1024,
                    'process_tree_peak_bytes': None}}

    def test_verified_unate_region_recounts_from_its_witness(self):
        report, records = fixture()
        result = summarize(report, records, Budget(1000000))
        self.assertEqual(result['status'], 'verified_structural_evidence')
        self.assertEqual(result['totals']['verified_unate_regions'], 1)
        self.assertEqual(result['totals']['unate'], 1)
        self.assertEqual(result['totals']['abc_fallback'], 1)
        self.assertEqual(result['archive_records'], {'sources': 1, 'decompositions': 1})
        unate = result['regions'][0]
        self.assertEqual(unate['structure']['function_producers'], report['regions_searched'][0]['functions'])

    def test_tampered_counts_bindings_and_totals_fail(self):
        for mutation in ('functions', 'twins', 'literals', 'ports', 'inverters', 'variant', 'recipe_index',
                         'status', 'fallback_reason', 'fallback_functions', 'totals', 'duplicate', 'unreferenced'):
            report, records = fixture()
            unate, fallback = report['regions_searched']
            if mutation == 'functions': unate['functions'] += 1
            elif mutation == 'twins': unate['twins'] += 1
            elif mutation == 'literals': unate['literals'] -= 1
            elif mutation == 'ports': unate['ports'] += 1
            elif mutation == 'inverters': unate['source_inverters'] += 1
            elif mutation == 'variant': unate['attempts'][0]['variant'] = 'initial'
            elif mutation == 'recipe_index': unate['attempts'][0]['recipe_index'] = 1
            elif mutation == 'status': unate['status'] = 'selected'
            elif mutation == 'fallback_reason': fallback['reason'] = ''
            elif mutation == 'fallback_functions': fallback['functions'] = 3
            elif mutation == 'totals': report['totals']['functions'] += 1
            elif mutation == 'duplicate': fallback['region'] = 'r'
            else:
                unate['attempts'][0]['witness'] = {'record': -1, 'status': 'not_constructed'}
            with self.subTest(mutation=mutation), self.assertRaises((Invalid, KeyError)):
                summarize(report, records, Budget(1000000))

    def test_missing_selected_witness_is_incomplete_not_verified(self):
        report, records = fixture()
        report['witness_archive']['records'] = 1
        report['regions_searched'][0]['attempts'][0]['witness'] = {'record': -1, 'status': 'size_limit'}
        result = summarize(report, records[:1], Budget(1000000))
        self.assertEqual(result['status'], 'incomplete')
        self.assertNotIn('verified_unate_regions', result['totals'])

    def test_reused_region_is_historical_and_counts_once(self):
        report, records = fixture()
        row = report['regions_searched'].pop(0)
        report['regions_reused'] = [{'region': 'r', 'cached_region': 'r', 'metrics_scope': 'historical_search',
                                     'decision': row}]
        report['totals'].update(reused=1, unate=0, functions=0, twins=0, literals=0)
        result = summarize(report, records, Budget(1000000))
        self.assertEqual(result['regions'][1]['metrics_scope'], 'historical_search')
        self.assertEqual(result['totals']['verified_unate_regions'], 1)

    def test_invocation_cost_requires_matching_report_and_honest_memory_scope(self):
        report, records = fixture()
        envelope = self.invocation(report)
        result = summarize(report, records, Budget(1000000), envelope)
        self.assertEqual(result['reported_invocation']['wall_ms'], 10)
        self.assertEqual(result['reported_invocation']['parent_peak_rss_bytes'], 1024)
        self.assertIsNone(result['reported_invocation']['process_tree_peak_bytes'])
        self.assertFalse(result['resource_metrics_independently_verified'])
        for command in ('synth verilog', 'synth pyrope'):
            envelope['command'] = command
            self.assertEqual(summarize(report, records, Budget(1000000), envelope)['reported_invocation']['wall_ms'], 10)
        envelope['synthesis_invocation']['parent_peak_rss_bytes'] = None
        result = summarize(report, records, Budget(1000000), envelope)
        self.assertIsNone(result['reported_invocation']['parent_peak_rss_bytes'])
        del envelope['synthesis_invocation']
        self.assertIsNone(summarize(report, records, Budget(1000000), envelope)['reported_invocation'])

    def test_invocation_mismatch_failure_and_inconsistent_costs_are_rejected(self):
        for mutation in ('report', 'report_type', 'failed', 'tree', 'scope', 'time', 'phases', 'rss_bool', 'rss_zero', 'time_nan', 'schema', 'command'):
            report, records = fixture()
            envelope = self.invocation(report)
            observation = envelope['synthesis_invocation']
            if mutation == 'report': envelope['qor']['synth']['totals']['functions'] += 1
            elif mutation == 'report_type': envelope['qor']['synth']['schema_version'] = True
            elif mutation == 'failed': envelope['status'] = 'fail'
            elif mutation == 'tree': observation['process_tree_peak_bytes'] = 2048
            elif mutation == 'scope': observation['scope'] = 'region_search'
            elif mutation == 'time': observation['wall_ms'] = -1
            elif mutation == 'phases': envelope['phases'][0]['ms'] = 11
            elif mutation == 'rss_bool': observation['parent_peak_rss_bytes'] = True
            elif mutation == 'rss_zero': observation['parent_peak_rss_bytes'] = 0
            elif mutation == 'time_nan': observation['wall_ms'] = float('nan')
            elif mutation == 'schema': envelope['schema_version'] = True
            else: envelope['command'] = 'lec'
            with self.subTest(mutation=mutation), self.assertRaises(Invalid):
                summarize(report, records, Budget(1000000), envelope)

    def test_budget_exhaustion_is_inconclusive(self):
        report, records = fixture()
        with self.assertRaises(Exhausted):
            summarize(report, records, Budget(0))


if __name__ == '__main__':
    unittest.main()
