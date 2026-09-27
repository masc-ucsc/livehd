#!/usr/bin/env python3
"""Compare shared semantic identities in baseline and refactored legacy exports.

Compares NodeCert values, topo/source IDs, source widths and expressions, output
projections, and next-state dependency IDs/types. It deliberately does not claim
that arbitrary Lean expressions are equivalent; the tiny Lean oracles check the
changed fast expressions and the reset/ROM capability expansion separately.
"""
import json
from pathlib import Path
import re
import sys
from compare_exports import summarize


def structure(text):
    basic = summarize(text)
    result = {key: basic[key] for key in ('nodes', 'topo', 'sources')}
    source = re.search(r'def \w+_sourceEnv .*?: Nat -> .*?:= fun n =>\n(.*?)(?=\ndef )', text, re.S)
    result['source_values'] = {}
    if source:
        for sid, expr in re.findall(r'if n = (\d+) then (.*?) else\n', source[1]):
            # Parentheses and whitespace only; do not normalize identifiers,
            # arithmetic, signs, widths, values, or memory/bitvector constructors.
            result['source_values'][sid] = re.sub(r'[\s()]', '', expr)
    projection = re.search(r'def \w+_outputsFromCert .*?:=\n(.*?)(?=\ndef |\ntheorem )', text, re.S)
    result['outputs'] = re.sub(r'[\s()]', '', projection[1]) if projection else ''
    state = re.search(r'def \w+_nextStateFromCert .*?:=\n(.*?)(?=\ndef )', text, re.S)
    result['next_state'] = {}
    if state:
        for field, ty, expr in re.findall(r'let new_(\w+) : (.*?) := (.*)', state[1]):
            result['next_state'][field] = {
                'type': re.sub(r'\s', '', ty),
                'dependencies': re.findall(r'rho (\d+)', expr),
                'memory': 'memdec' in expr,
            }
    return result


def main():
    before, after, destination = map(Path, sys.argv[1:])
    old = json.loads((before / 'results.json').read_text())
    new = {(r['group'], r['top']): r for r in json.loads((after / 'results.json').read_text())}
    results = []
    for row in old:
        if 'sha256' not in row:
            continue
        other = new[(row['group'], row['top'])]
        if 'sha256' not in other:
            raise SystemExit('previously accepted graph now refuses: ' + row['top'])
        rel = Path(row['group']) / row['top'] / 'export' / (row['top'] + '_Lgraph.lean')
        a, b = structure((before / rel).read_text()), structure((after / rel).read_text())
        changed = [key for key in a if a[key] != b[key]]
        results.append({'group': row['group'], 'top': row['top'], 'changed': changed})
    destination.write_text(json.dumps(results, indent=2) + '\n')
    failed = [r for r in results if r['changed']]
    print('compared', len(results), 'differences', failed)
    if failed:
        sys.exit(1)


if __name__ == '__main__':
    main()
