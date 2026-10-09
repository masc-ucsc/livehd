#!/usr/bin/env python3
"""Inspect a saved graph before Lean lowering; requires lean_graph_census.

Example: raw_graph_census.py --tool bazel-bin/pass/lean/lean_graph_census \
    --graph generated/example/lgdb --top Example --output generated/example/census.json

An optional --compile-log supplies native one-hot diagnostics from the SAME
compile. The graph's runtime_check attribute alone cannot distinguish deferred
checks from refutations. This tool never runs or changes the native prover.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess


def collect(tool, graph, top, compile_log=None):
    run = subprocess.run([str(tool.resolve()), str(graph.resolve()), top],
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         timeout=120, check=True)
    nodes = {}
    for line in run.stdout.splitlines():
        row = line.split('\t')
        if row[0] == 'schema':
            if row[1] != 'lean_graph_census_v1':
                raise ValueError('unsupported raw graph census schema')
        elif row[0] == 'node':
            nodes[int(row[1])] = dict(id=int(row[1]), op=row[2], width=int(row[3]),
                                     signed=bool(int(row[4])), operands=[])
        elif row[0] == 'operand':
            nodes[int(row[1])]['operands'].append(dict(
                port=int(row[2]), bank=int(row[3]), width=int(row[4]),
                signed=bool(int(row[5])), kind=row[6], intrinsic_width=int(row[7]),
                value=row[8]))
        elif row[0] == 'memory':
            nodes[int(row[1])].setdefault('memory_pins', []).append(dict(
                name=row[2], kind=row[3], width=int(row[4]), value=row[5]))
        elif row[0] == 'onehot':
            nodes[int(row[1])]['onehot_graph_status'] = row[2]
        else:
            raise ValueError('unexpected raw census record: ' + line)
    if not run.stdout.startswith('schema\tlean_graph_census_v1\n'):
        raise ValueError('missing raw census schema')
    codes = Counter()
    if compile_log is not None:
        for line in compile_log.read_text().splitlines():
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict) and obj.get('code', '').startswith('onehot-'):
                codes[obj['code']] += 1
    statuses = Counter(n['onehot_graph_status'] for n in nodes.values() if n['op'] == 'hotmux')
    if codes['onehot-violated']:
        verdict = 'refuted'
    elif not statuses:
        verdict = 'not-applicable'
    elif codes['onehot-deferred']:
        verdict = 'deferred'
    elif statuses['runtime-check']:
        verdict = 'runtime-check-unclassified'
    elif statuses['unreported']:
        verdict = 'unreported'
    else:
        verdict = 'native-proved'
    return dict(schema_version=1, top=top, graph_stage='before Lean lowering',
                node_count=len(nodes), operators=dict(sorted(Counter(n['op'] for n in nodes.values()).items())),
                onehot=dict(status=verdict, graph_attributes=dict(statuses), diagnostic_codes=dict(codes),
                            lean_proves_onehot=False,
                            note='Native proof attributes are provenance, not Lean theorems. Refuted compilation is a failure.'),
                nodes=[nodes[n] for n in sorted(nodes)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', required=True, type=Path)
    parser.add_argument('--graph', required=True, type=Path)
    parser.add_argument('--top', required=True)
    parser.add_argument('--compile-log', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = collect(args.tool, args.graph, args.top, args.compile_log)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'nodes'}, sort_keys=True))


if __name__ == '__main__':
    main()
