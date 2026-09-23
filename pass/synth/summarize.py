#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Recompute the structural result of a pass.synth run from its saved evidence.

Checks every archived source and unate decomposition, binds each unate region to
the decomposition it selected, and recounts that region's unate functions,
negative-rail functions, literals, ports and source inverters from the witness.
Does not rerun technology mapping, equivalence checking (`lhd lec`) or timing:
reported physical metrics stay separate from independently checked data.
"""
import argparse
import collections
import json
import math
import pathlib
import sys

from check_witness import Budget, Exhausted, Invalid, check, check_source, require


def source_metrics(source, budget):
    nodes, outputs = source['nodes'], source['outputs']
    depends = []
    for node in nodes:
        budget.spend(len(node['inputs']) + 1)
        depends.append(node['source'] or any(depends[i] for i in node['inputs']))
    reachable = set(outputs)
    for index in reversed(range(len(nodes))):
        budget.spend(len(nodes[index]['inputs']) + 1)
        if index in reachable:
            reachable.update(nodes[index]['inputs'])
    direct = sum(nodes[i]['source'] for i in outputs)
    constants = sum(not depends[i] for i in outputs)
    logic = {i for i in outputs if depends[i] and not nodes[i]['source']}
    return {'outputs': len(outputs), 'unique_outputs': len(set(outputs)),
            'direct_source_outputs': direct, 'source_free_outputs': constants,
            'logic_outputs': len(outputs) - direct - constants, 'unique_logic_outputs': len(logic),
            'reachable_source_nodes': sum(nodes[i]['source'] for i in reachable),
            'reachable_function_nodes': sum(not nodes[i]['source'] for i in reachable)}


def network_metrics(network, budget):
    nodes, outputs = network['nodes'], network['outputs']
    reads = collections.Counter(outputs)
    polarities = collections.defaultdict(set)
    reachable = set(outputs)
    for node in nodes:
        budget.spend(len(node['ports']) + 1)
        reads.update(node['ports'])
    for index in reversed(range(len(nodes))):
        budget.spend(len(nodes[index]['ports']) + 1)
        if index in reachable:
            reachable.update(nodes[index]['ports'])
    functions = [i for i, n in enumerate(nodes) if n['kind'] == 'function']
    for i in functions:
        polarities[nodes[i]['origin']].add(nodes[i]['negative'])
    used = len(set(functions) & reachable)
    return {'function_producers': len(functions),
            'negative_function_producers': sum(nodes[i]['negative'] for i in functions),
            'source_inverters': sum(n['kind'] == 'source_inverter' for n in nodes),
            'twin_pairs': sum(len(rails) == 2 for rails in polarities.values()),
            'shared_function_producers': sum(reads[i] > 1 for i in functions),
            'function_reads': sum(reads[i] for i in functions), 'endpoint_reads': len(outputs),
            'literal_occurrences': sum(len(t) for i in functions for t in nodes[i]['terms']),
            'function_ports': sum(len(nodes[i]['ports']) for i in functions),
            'reachable_function_producers': used, 'unused_function_producers': len(functions) - used}


def nonnegative_number(value, name):
    require(type(value) in (int, float) and math.isfinite(value) and value >= 0, 'invalid ' + name)
    return value


def same_counts(reported, expected, message):
    require(type(reported) is dict and reported.keys() == expected.keys()
            and all(type(value) is int and value == expected[key] for key, value in reported.items()), message)


def check_invocation(report, envelope, budget):
    if envelope is None:
        return None
    require(type(envelope) is dict and type(envelope.get('schema_version')) is int
            and envelope['schema_version'] == 1 and envelope.get('tool') == 'lhd'
            and envelope.get('command') in ('synth', 'synth verilog', 'synth pyrope') and envelope.get('status') == 'pass'
            and type(envelope.get('exit_code')) is int and envelope['exit_code'] == 0,
            'expected a successful synthesis invocation envelope')
    require(type(envelope.get('qor')) is dict and 'synth' in envelope['qor'], 'invocation/report binding mismatch')
    # Python considers True == 1 and 1 == 1.0. Bind JSON types as well as values,
    # without generating two extra serialized copies of a potentially large report.
    pending = [(report, envelope['qor']['synth'])]
    while pending:
        budget.spend()
        expected, actual = pending.pop()
        require(type(expected) is type(actual), 'invocation/report binding mismatch')
        if type(expected) is dict:
            require(expected.keys() == actual.keys(), 'invocation/report binding mismatch')
            pending.extend((value, actual[key]) for key, value in expected.items())
        elif type(expected) is list:
            require(len(expected) == len(actual), 'invocation/report binding mismatch')
            pending.extend(zip(expected, actual))
        else:
            require(expected == actual, 'invocation/report binding mismatch')
    if 'synthesis_invocation' not in envelope:
        return None  # A matching older envelope has no enclosing observation.
    value = envelope.get('synthesis_invocation')
    require(type(value) is dict and value.get('scope') == 'main_entry_to_result_emission'
            and value.get('memory_scope') == 'parent_process_peak_rss'
            and 'process_tree_peak_bytes' in value and value['process_tree_peak_bytes'] is None,
            'unsupported invocation observation scope')
    elapsed = nonnegative_number(value['wall_ms'], 'invocation time')
    peak = value['parent_peak_rss_bytes']
    require(peak is None or (type(peak) is int and 0 < peak <= (1 << 64) - 1), 'invalid parent peak RSS')
    phases = envelope.get('phases', [])
    require(type(phases) is list, 'invalid invocation phases')
    total = 0
    for phase in phases:
        budget.spend()
        require(type(phase) is dict and type(phase.get('name')) is str, 'invalid invocation phase')
        total += nonnegative_number(phase['ms'], 'invocation phase time')
    # CLI phase rows are rounded to 0.001 ms; the enclosing timer is not.
    require(total <= elapsed + len(phases) * 0.0005 + 1e-6, 'phase times exceed invocation interval')
    return dict(value, phases=phases, status=envelope['status'], run_id=envelope.get('run_id'))


def summarize(report, records, budget, invocation=None):
    require(type(report.get('schema_version')) is int and report['schema_version'] == 2
            and report.get('kind') == 'synth', 'unsupported synthesis report')
    invocation_observation = check_invocation(report, invocation, budget)
    by_id, verified = {}, {}
    for index, record in enumerate(records):
        budget.spend()
        require(type(record.get('record')) is int and record['record'] == index, 'archive record order mismatch')
        require(isinstance(record.get('region'), str), 'missing record region')
        by_id[index] = record
        if record.get('kind') == 'unate_source':
            require(type(record.get('schema_version')) is int and record['schema_version'] in (1, 2), 'invalid source schema')
            check_source(record, budget)
        else:
            check(record, budget)
            verified[index] = network_metrics(record['network'], budget)
    require(report['witness_archive']['records'] == len(records), 'archive/report record count mismatch')
    missing, regions, used, seen = [], [], set(), set()
    counts = collections.Counter()

    def evidence(reference, kind, region, label):
        require(isinstance(reference, dict), 'missing witness reference')
        record_id = reference['record']
        require(type(record_id) is int, 'invalid witness id')
        if reference['status'] != 'archived':
            require(record_id == -1, 'unarchived witness has an id')
            if reference['status'] != 'not_constructed':
                missing.append({'region': region, 'evidence': label, 'reason': reference['status']})
            return None
        require(record_id in by_id and record_id not in used, 'missing or multiply referenced witness')
        record = by_id[record_id]
        require(record['kind'] == kind and record['region'] == region, 'witness kind/region binding mismatch')
        used.add(record_id)
        return record_id

    rows = [(row['region'], row, 'current_search') for row in report['regions_searched']]
    for wrapper in report['regions_reused']:
        require(wrapper['metrics_scope'] == 'historical_search', 'reused metrics must be historical')
        require(wrapper['cached_region'] == wrapper['decision']['region'], 'cached region mismatch')
        rows.append((wrapper['region'], wrapper['decision'], 'historical_search'))
    for name, row, scope in rows:
        budget.spend()
        require(name not in seen, 'region decided twice')
        seen.add(name)
        status = row['status']
        require(status in ('unate', 'abc_fallback'), 'invalid region status')
        source_id = evidence(row.get('source_witness', {'record': -1, 'status': 'unavailable'}), 'unate_source', name, 'source')
        denominator = None
        if source_id is not None:
            denominator = source_metrics(by_id[source_id]['source'], budget)
            same_counts(row['source_metrics'], denominator, 'source denominator mismatch')
        structure = None
        for index, attempt in enumerate(row['attempts']):
            budget.spend()
            require(attempt['recipe_index'] == index, 'attempt order mismatch')
            record_id = evidence(attempt['witness'], 'unate_witness', name, f'attempt:{index}')
            if record_id is None:
                continue
            require(structure is None, 'more than one selected decomposition')
            record = by_id[record_id]
            require(record['recipe_index'] == index and record['variant'] == attempt['variant']
                    and record['recipe'] == attempt['recipe'], 'attempt/witness binding mismatch')
            require(row['variant'] == attempt['variant'], 'selected variant mismatch')
            structure = verified[record_id]
        if status == 'unate':
            if structure is None:
                missing.append({'region': name, 'evidence': 'selected decomposition', 'reason': 'not_archived'})
            else:
                for field, key in (('functions', 'function_producers'), ('twins', 'negative_function_producers'),
                                   ('literals', 'literal_occurrences'), ('ports', 'function_ports'),
                                   ('source_inverters', 'source_inverters')):
                    require(type(row[field]) is int and row[field] == structure[key], 'region ' + field + ' mismatch')
                counts['verified_unate_regions'] += 1
            counts['unate'] += 1
        else:
            require(structure is None and row['functions'] == 0, 'fallback region carries a selected decomposition')
            require(isinstance(row['reason'], str) and row['reason'], 'fallback without a reason')
            counts['abc_fallback'] += 1
        if scope == 'current_search':
            for field in ('functions', 'twins', 'literals'):
                counts[field] += row[field]
        regions.append({'region': name, 'metrics_scope': scope, 'status': status, 'source': denominator,
                        'structure': structure, 'reported_reason': row['reason']})
    require(used == set(by_id), 'unreferenced archive records')
    totals = report['totals']
    require(totals['regions'] == len(rows) and totals['reused'] == len(report['regions_reused'])
            and totals['unate'] + totals['abc_fallback'] == len(report['regions_searched']), 'report region totals mismatch')
    searched = [row for row in report['regions_searched']]
    require(totals['unate'] == sum(row['status'] == 'unate' for row in searched)
            and all(totals[field] == counts[field] for field in ('functions', 'twins', 'literals')), 'report totals mismatch')
    return {'schema_version': 2, 'kind': 'unate_evaluation',
            'status': 'incomplete' if missing else 'verified_structural_evidence',
            'verification_scope': 'source_denominators_and_archived_boolean_decompositions',
            'archive_records': {'sources': len(records) - len(verified), 'decompositions': len(verified)},
            'physical_metrics_independently_verified': False,
            'resource_metrics_independently_verified': False,
            'reported_invocation': invocation_observation,
            'totals': dict(counts), 'regions': regions, 'missing_evidence': missing}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('archive', type=pathlib.Path)
    parser.add_argument('--invocation-result', type=pathlib.Path,
                        help='matching successful lhd synth --result-json envelope for enclosing command costs')
    parser.add_argument('--work', type=int, default=50000000)
    parser.add_argument('--max-bytes', type=int, default=64 * 1024 * 1024)
    args = parser.parse_args()
    try:
        require(args.work >= 0 and args.max_bytes >= 0, 'negative verification bound')
        paths = [args.report, args.archive] + ([args.invocation_result] if args.invocation_result else [])
        if sum(path.stat().st_size for path in paths) > args.max_bytes:
            raise Exhausted('report/archive size limit')
        report = json.loads(args.report.read_text())
        require(report['witness_archive']['bytes'] == args.archive.stat().st_size, 'archive/report byte count mismatch')
        records = [json.loads(line) for line in args.archive.open()]
        invocation = json.loads(args.invocation_result.read_text()) if args.invocation_result else None
        result = summarize(report, records, Budget(args.work), invocation)
        print(json.dumps(result, sort_keys=True, allow_nan=False))
        return 0 if result['status'] == 'verified_structural_evidence' else 2
    except Exhausted as error:
        print('inconclusive: ' + str(error), file=sys.stderr)
        return 2
    except (Invalid, OSError, KeyError, TypeError, ValueError, IndexError) as error:
        print('invalid synthesis evidence: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
