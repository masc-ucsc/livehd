#!/usr/bin/env python3
"""Append one machine-readable record for a `--file-ab` run.

Durability is the point: a number in prose cannot be re-identified later, and
"the ALU passed" is meaningless without the certificate, the code, the budget
and the category it passed under.  Each record pins all four.

    python3 scripts/record_experiment.py LOG.log CERT.dcert \
        --provenance "CVA6 alu.sv via the cva6_alu_export wrapper" \
        --commit 4f64d6aad [--binary .native-dev/proto_probe]

Appends one JSON object per line to
LeanSemanticPrimitives/Projection/certio/experiments.jsonl
"""
import argparse, hashlib, json, os, re, subprocess, sys, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
LEAN = os.path.dirname(HERE)
OUT = os.path.join(LEAN, 'LeanSemanticPrimitives', 'Projection', 'certio',
                   'experiments.jsonl')
# the two sources that decide what the run actually did
CODE = ['scripts/proto_probe.lean',
        'LeanSemanticPrimitives/Projection/Proto/InterpreterVariant.lean',
        'LeanSemanticPrimitives/Projection/Proto/PartialEvaluatorFast.lean']


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def num(pat, text, cast=int, default=None):
    m = re.search(pat, text)
    return cast(m.group(1)) if m else default


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('cert')
    ap.add_argument('--provenance', required=True)
    ap.add_argument('--commit', default=None)
    ap.add_argument('--binary', default=None)
    a = ap.parse_args()

    t = open(a.log).read()
    exit_code = num(r'exit (\d+)\s*$', t)
    if exit_code is None:
        exit_code = 0 if 'STAGE TIMES' in t else None

    seeds = re.findall(r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)', t)
    stage = re.search(
        r'STAGE TIMES ms: specialize (\d+)\s+checkResidual (\d+)\s+'
        r'reference-runs (\d+) \((\d+)\)\s+residual-runs (\d+) \((\d+)\)\s+control (\d+)', t)

    rec = {
        'recorded_utc': datetime.datetime.now(datetime.timezone.utc)
                                .strftime('%Y-%m-%dT%H:%M:%SZ'),
        'design': os.path.basename(a.cert),
        'cert_sha256': sha(a.cert),
        'cert_bytes': os.path.getsize(a.cert),
        'provenance': a.provenance,
        'provenance_note': 'a CONFIGURED EXPORT WRAPPER of one module, not '
                           'arbitrary or whole-core CVA6',
        'code': {p: sha(os.path.join(LEAN, p)) for p in CODE
                 if os.path.exists(os.path.join(LEAN, p))},
        'git_commit': a.commit or subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'], cwd=LEAN,
            capture_output=True, text=True).stdout.strip() or None,
        'binary_sha256': sha(os.path.join(LEAN, a.binary)) if a.binary else None,
        'specializer': 'Projection.ProtoFast.mixDriver  (FORK -- PRes.val has no bridge)',
        'interpreter': 'Projection.ProtoVar.hwAPVar  (VARIANT -- no equivalence lemma)',
        'category': 'EXPERIMENTAL: certificate-relative execution agreement. '
                    'NOT theorem-covered (the proved path is mixDriver + hwAP, '
                    'which was not run) and NOT RTL equivalence.',
        'fuel': {'step': num(r'fuel (\d+)/', t), 'work': num(r'fuel \d+/(\d+)', t)},
        'stock_fuel': {'step': 20000, 'work': 200},
        'design_shape': {
            'sources': num(r'sources (\d+) nodes', t),
            'nodes': num(r'nodes (\d+) flops', t),
            'flops': num(r'flops (\d+)', t)},
        'support_all_six': 'support:' in t and 'false' not in (
            re.search(r'support: (.*)', t).group(1) if re.search(r'support: (.*)', t) else 'false'),
        'residual': {
            'terms': num(r'terms (\d+)', t),
            'tl': num(r'prims: tl (\d+)', t),
            'consP': num(r'consP (\d+)', t)},
        'check_residual_bound': num(r'proved-sufficient bound (\d+)', t)
                                 or num(r'ACCEPTED, exact fuel (\d+)', t),
        'bound_meaning': 'height; checkResidual_sound proves no outOfFuel at it. '
                         'NOT a minimum for any given input.',
        'stimuli': {
            'count': len(seeds),
            'kinds': 'seeds 0-4 are width-aware corners (zero, all-ones, sign '
                     'bit, all-but-sign-bit, alternating) at each input\'s '
                     'widest declared use; 7 is pseudorandom',
            'all_match_interpretDesign': all(x[2] == 'true' for x in seeds) if seeds else None},
        'trace': re.search(r'trace: (.*)', t).group(1).strip() if re.search(r'trace: ', t) else None,
        'stage_times_ms': ({
            'specialize': int(stage.group(1)), 'check_residual': int(stage.group(2)),
            'reference_runs_total': int(stage.group(3)), 'reference_runs_n': int(stage.group(4)),
            'residual_runs_total': int(stage.group(5)), 'residual_runs_n': int(stage.group(6)),
            'control': int(stage.group(7))} if stage else None),
        'wall_s': num(r'wall ([\d.]+) s', t, float),
        'peak_rss_kb': num(r'RSS (\d+) KB', t),
        'exit': exit_code,
        'log': os.path.relpath(os.path.abspath(a.log),
                               os.path.dirname(os.path.dirname(LEAN))),
    }
    with open(OUT, 'a') as f:
        f.write(json.dumps(rec, sort_keys=True) + '\n')
    print(f'appended {rec["design"]} (exit {rec["exit"]}) to {OUT}')


if __name__ == '__main__':
    main()
