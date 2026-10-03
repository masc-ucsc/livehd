#!/usr/bin/env python3
"""Record one `--file-ab` run in a machine-readable manifest.

The point is that a number in prose cannot be re-identified later.  That only
works if the record says what was ACTUALLY executed, so this script
distinguishes three strengths of evidence and never upgrades one to another:

  captured-pre-launch   hashes taken BEFORE the process started -- the only
                        form that pins the executed code without assumption
  captured-during-run   hashes taken while the process ran, including
                        /proc/<pid>/exe, which is evidence the running image
                        is that file
  reconstructed         hashes taken at record time, AFTER the run; these
                        describe the tree now, NOT what ran, and are labelled
                        so

Usage:
  # BEFORE launching -- this is the strong form
  record_experiment.py prelaunch CERT.dcert --binary .native-dev/proto_probe \
      --cmd "..." --out capture.json

  # AFTER it exits
  record_experiment.py record LOG.log CERT.dcert \
      --provenance "..." [--capture capture.json]

  record_experiment.py selftest        # regressions for the parsers
"""
import argparse, hashlib, json, os, re, subprocess, sys, datetime, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LEAN = os.path.dirname(HERE)
OUT = os.path.join(LEAN, 'LeanSemanticPrimitives', 'Projection', 'certio',
                   'experiments.jsonl')
CODE = ['scripts/proto_probe.lean',
        'LeanSemanticPrimitives/Projection/Proto/InterpreterVariant.lean',
        'LeanSemanticPrimitives/Projection/Proto/PartialEvaluatorFast.lean']
SUPPORT_FIELDS = ['wf', 'memFree', 'sources', 'ops', 'arities', 'flopClocks']


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def git(*args):
    r = subprocess.run(['git'] + list(args), cwd=LEAN, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def tree_state():
    dirty = git('status', '--porcelain') or ''
    diff = git('diff', 'HEAD') or ''
    return {
        'git_rev': git('rev-parse', 'HEAD'),
        'dirty_file_count': len([l for l in dirty.splitlines() if l.strip()]),
        'diff_sha256': hashlib.sha256(diff.encode()).hexdigest(),
        'source_sha256': {p: sha(os.path.join(LEAN, p)) for p in CODE
                          if os.path.exists(os.path.join(LEAN, p))},
    }


def num(pat, text, cast=int, default=None):
    m = re.search(pat, text)
    return cast(m.group(1)) if m else default


# --------------------------------------------------------------------------
def parse_exit(t):
    """Exit status, or UNKNOWN.  Never inferred.

    `--file-ab` prints STAGE TIMES and still returns 1 when a comparison
    disagreed, so the presence of that line says nothing about the exit code.
    An earlier version of this script read it as success; that invented a
    result."""
    m = re.search(r'^exit (\d+)\s*$', t, re.M) or re.search(r'exit (\d+)\s*$', t)
    if m:
        return int(m.group(1)), 'from /usr/bin/time trailer'
    if re.search(r'Command exited with non-zero status (\d+)', t):
        return (int(re.search(r'Command exited with non-zero status (\d+)', t).group(1)),
                'from /usr/bin/time message')
    return None, 'MISSING -- the log carries no exit evidence; status is UNKNOWN'


def parse_support(t):
    """All six fields must be present AND true.  Absence of the word 'false'
    is not the same thing: a truncated line has no 'false' in it either."""
    m = re.search(r'support: (.*)', t)
    if not m:
        return {'all_six_true': None, 'fields': None,
                'note': 'no support line in the log'}
    line = m.group(1)
    fields = {}
    for f in SUPPORT_FIELDS:
        fm = re.search(rf'\b{f} (true|false)\b', line)
        fields[f] = (fm.group(1) == 'true') if fm else None
    return {'all_six_true': all(fields[f] is True for f in SUPPORT_FIELDS),
            'fields': fields,
            'note': None if all(v is not None for v in fields.values())
                    else 'one or more fields absent from the log'}


def parse_seeds(t):
    seeds = re.findall(r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)', t)
    return {
        'count': len(seeds),
        'kinds': "seeds 0-4 are width-aware corners (zero, all-ones, sign bit, "
                 "all-but-sign-bit, alternating) at each input's widest declared "
                 "use; 7 and up are pseudorandom",
        'per_seed': [{'seed': int(s), 'outcome': o, 'matches': m == 'true'}
                     for s, o, m in seeds],
        'all_match_interpretDesign': (all(m == 'true' for _, _, m in seeds)
                                      if seeds else None),
    }


# --------------------------------------------------------------------------
def cmd_prelaunch(a):
    cap = {
        'captured_utc': datetime.datetime.now(datetime.timezone.utc)
                                .strftime('%Y-%m-%dT%H:%M:%SZ'),
        'evidence': 'captured-pre-launch',
        'command': a.cmd,
        'cert_sha256': sha(a.cert),
        'binary_sha256': sha(os.path.join(LEAN, a.binary)) if a.binary else None,
        'binary_path': a.binary,
        **tree_state(),
    }
    with open(a.out, 'w') as f:
        json.dump(cap, f, indent=1, sort_keys=True)
    print(f'pre-launch capture written to {a.out}')


def cmd_record(a):
    t = open(a.log).read()
    exit_code, exit_evidence = parse_exit(t)
    stage = re.search(
        r'STAGE TIMES ms: specialize (\d+)\s+checkResidual (\d+)\s+'
        r'reference-runs (\d+) \((\d+)\)\s+residual-runs (\d+) \((\d+)\)\s+control (\d+)', t)
    cap = json.load(open(a.capture)) if a.capture else None

    if cap:
        code_identity = dict(cap)
        code_identity['describes'] = 'the code that ran'
    else:
        code_identity = {
            'evidence': 'reconstructed-at-record-time',
            'describes': 'the WORKING TREE WHEN THIS RECORD WAS WRITTEN, which '
                         'is NOT evidence about what executed; the binary that '
                         'ran was not hashed and is UNKNOWN',
            'binary_sha256': None,
            **tree_state(),
        }

    rec = {
        'recorded_utc': datetime.datetime.now(datetime.timezone.utc)
                                .strftime('%Y-%m-%dT%H:%M:%SZ'),
        'design': os.path.basename(a.cert),
        'cert_sha256': sha(a.cert),
        'cert_bytes': os.path.getsize(a.cert),
        'log': os.path.relpath(os.path.abspath(a.log),
                               os.path.dirname(os.path.dirname(LEAN))),
        'log_sha256': sha(a.log),
        'provenance': a.provenance,
        'provenance_note': 'a CONFIGURED EXPORT WRAPPER of one module, not '
                           'arbitrary or whole-core CVA6',
        'code_identity': code_identity,
        'specializer': 'Projection.ProtoFast.mixDriver  (FORK -- PRes.val has no bridge)',
        'interpreter': 'Projection.ProtoVar.hwAPVar  (VARIANT -- no equivalence lemma)',
        'category': 'EXPERIMENTAL: certificate-relative execution agreement. '
                    'NOT theorem-covered (the proved path is mixDriver + hwAP, '
                    'which was not run) and NOT RTL equivalence.',
        'fuel': {'step': num(r'fuel (\d+)/', t), 'work': num(r'fuel \d+/(\d+)', t)},
        'stock_fuel': {'step': 20000, 'work': 200},
        'design_shape': {'sources': num(r'sources (\d+) nodes', t),
                         'nodes': num(r'nodes (\d+) flops', t),
                         'flops': num(r'flops (\d+)', t)},
        'support': parse_support(t),
        'residual': {'terms': num(r'terms (\d+)', t),
                     'tl': num(r'prims: tl (\d+)', t),
                     'consP': num(r'consP (\d+)', t)},
        'check_residual_bound': num(r'proved-sufficient bound (\d+)', t)
                                 or num(r'ACCEPTED, exact fuel (\d+)', t),
        'bound_meaning': 'height; checkResidual_sound proves no outOfFuel at it. '
                         'NOT a minimum for any given input.',
        'stimuli': parse_seeds(t),
        'trace': (re.search(r'trace: (.*)', t).group(1).strip()
                  if re.search(r'trace: ', t) else None),
        'control': (re.search(r'control: (.*)', t).group(1).strip()
                    if re.search(r'control: ', t) else None),
        'stage_times_ms': ({'specialize': int(stage.group(1)),
                            'check_residual': int(stage.group(2)),
                            'reference_runs_total': int(stage.group(3)),
                            'reference_runs_n': int(stage.group(4)),
                            'residual_runs_total': int(stage.group(5)),
                            'residual_runs_n': int(stage.group(6)),
                            'control': int(stage.group(7))} if stage else None),
        'wall_s': num(r'wall ([\d.]+) s', t, float),
        'peak_rss_kb': num(r'RSS (\d+) KB', t),
        'exit': exit_code,
        'exit_evidence': exit_evidence,
    }
    with open(OUT, 'a') as f:
        f.write(json.dumps(rec, sort_keys=True) + '\n')
    print(f'appended {rec["design"]} (exit {rec["exit"]!r}, {exit_evidence}) to {OUT}')


def cmd_selftest(_a):
    """Regressions for the two parsers that previously invented results."""
    fails = []

    def check(name, got, want):
        if got != want:
            fails.append(f'{name}: got {got!r}, want {want!r}')

    # 1. STAGE TIMES present but NO exit trailer -> UNKNOWN, never 0
    log = ("fuel 200000/2000\n"
           "  STAGE TIMES ms: specialize 1  checkResidual 2  reference-runs 3 (6)"
           "  residual-runs 4 (6)  control 5\n")
    check('exit/no-trailer', parse_exit(log)[0], None)

    # 2. a MISMATCH run that still prints STAGE TIMES and exits 1
    log1 = log + "wall 1.0 s RSS 10 KB exit 1\n"
    check('exit/mismatch', parse_exit(log1)[0], 1)

    # 3. truncated log
    check('exit/truncated', parse_exit("fuel 20000/200\n  [specialize ...")[0], None)

    # 4. support: a field missing is NOT all-true
    s = parse_support("support: wf true memFree true sources true ops true arities true\n")
    check('support/missing-field', s['all_six_true'], False)
    check('support/missing-flagged', s['fields']['flopClocks'], None)

    # 5. support: one false
    s = parse_support("support: wf true memFree false sources true ops true "
                      "arities true flopClocks true\n")
    check('support/one-false', s['all_six_true'], False)

    # 6. support: all six true
    s = parse_support("support: wf true memFree true sources true ops true "
                      "arities true flopClocks true\n")
    check('support/all-true', s['all_six_true'], True)

    # 7. no support line at all
    check('support/absent', parse_support("fuel 1/1\n")['all_six_true'], None)

    # 8. a seed that did NOT match must not read as all-match
    sd = parse_seeds("  seed 0: residual ok  matches interpretDesign true\n"
                     "  seed 1: residual ok  matches interpretDesign false\n")
    check('seeds/mismatch', sd['all_match_interpretDesign'], False)

    if fails:
        print('SELFTEST FAILED:'); [print('  ' + f) for f in fails]; return 1
    print('selftest: 8 checks passed')
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('prelaunch'); p.add_argument('cert')
    p.add_argument('--binary'); p.add_argument('--cmd', required=True)
    p.add_argument('--out', required=True); p.set_defaults(fn=cmd_prelaunch)
    r = sub.add_parser('record'); r.add_argument('log'); r.add_argument('cert')
    r.add_argument('--provenance', required=True); r.add_argument('--capture')
    r.set_defaults(fn=cmd_record)
    s = sub.add_parser('selftest'); s.set_defaults(fn=cmd_selftest)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == '__main__':
    main()
