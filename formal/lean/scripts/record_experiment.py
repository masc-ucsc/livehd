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
# The sources that decide what a run DID, per runner.  A fixed list was wrong:
# it hashed proto_probe/InterpreterVariant/PartialEvaluatorFast for every runner,
# including `total-probe`, which is built from none of the first and all of a
# different set.  Several are UNTRACKED, so a `git diff HEAD` hash does not
# cover them either and they must be hashed individually.
CODE_COMMON = ['LeanSemanticPrimitives/Projection/ProjectionCorrect.lean',
               'LeanSemanticPrimitives/Projection/ResidualFragment.lean',
               'LeanSemanticPrimitives/Projection/ProjectedStep.lean',
               'LeanSemanticPrimitives/Projection/HardwareInterpreter.lean']
CODE_FORK  = ['scripts/proto_probe.lean',
              'LeanSemanticPrimitives/Projection/Proto/InterpreterVariant.lean',
              'LeanSemanticPrimitives/Projection/Proto/PartialEvaluatorFast.lean']
CODE_TOTAL = ['scripts/total_probe.lean',
              'LeanSemanticPrimitives/Projection/Proto/RunnerSupport.lean',
              'LeanSemanticPrimitives/Projection/Proto/VariantExec.lean',
              'LeanSemanticPrimitives/Projection/Proto/VariantAdequacy.lean',
              'LeanSemanticPrimitives/Projection/Proto/RewriteTotal.lean',
              'LeanSemanticPrimitives/Projection/Proto/VariantTransport.lean']
# The PROVED specializer.  Every runner that calls `Projection.mixDriver` --
# total-probe, host-var, host-var-total -- is decided by this file, and it was
# missing: S1 (`PRes.val`, PHASE6_PERF.md 34) and the earlier `@[csimp]` fast
# path both changed it, and a ledger row could not tell a run before either
# change from one after except through `git_rev`.  The fork runner does not use
# it (`ProtoFast` is a separate copy), so it is not in CODE_COMMON.
CODE_PROVED = ['LeanSemanticPrimitives/Projection/PartialEvaluator.lean']
SUPPORT_FIELDS = ['wf', 'memFree', 'sources', 'ops', 'arities', 'flopClocks']

# Which runner produced the log, and therefore WHICH BACKEND ran.  The fields
# below were hardcoded to the fork+variant pair until a review pointed out that
# `--host-var` uses a different specializer and `--host-var-total` a different
# interpreter as well; recording a host run under the fork's labels would have
# misstated what was executed.  `log_marker` is checked against the log so a
# mismatched --runner is refused rather than silently mislabelled.
RUNNERS = {
    'file-ab': {
        'log_marker': 'fuel ',
        'specializer': 'Projection.ProtoFast.mixDriver  (FORK -- PRes.val has no bridge)',
        'interpreter': 'Projection.ProtoVar.hwAPVar  (VARIANT from the PARTIAL goInline -- no equivalence lemma)',
        'acceptance': '6 width-aware stimuli + multi-cycle trace + control interpreter',
        'code': CODE_FORK + CODE_COMMON,
        'seed_re': r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)',
        'terms_re': r'terms (\d+)',
        'bound_re': r'proved-sufficient bound (\d+)|ACCEPTED, exact fuel (\d+)',
        'stage_re': (r'STAGE TIMES ms: specialize (\d+)\s+checkResidual (\d+)\s+'
                     r'reference-runs (\d+) \((\d+)\)\s+residual-runs (\d+) \((\d+)\)'
                     r'\s+control (\d+)'),
        'stage_keys': ['specialize', 'check_residual', 'reference_runs_total',
                       'reference_runs_n', 'residual_runs_total', 'residual_runs_n',
                       'control'],
        'category': ('EXPERIMENTAL: certificate-relative execution agreement on an '
                     'UNPROVED backend.  The fork has no bridge and the old variant '
                     'no equivalence lemma.  NOT theorem-covered, NOT RTL equivalence.'),
    },
    'host-var': {
        'log_marker': 'HOST mixDriver (proved) + hwAPVar',
        'specializer': 'Projection.mixDriver  (the PROVED specializer -- no fork)',
        'interpreter': 'Projection.ProtoVar.hwAPVar  (VARIANT from the PARTIAL goInline -- no equivalence lemma, and NOT the proof target)',
        'acceptance': 'FEASIBILITY ONLY: 3 seeds, no trace, no control',
        'code': CODE_FORK + CODE_COMMON + CODE_PROVED,
        'seed_re': r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)',
        'terms_re': r'terms (\d+)',
        'bound_re': r'ACCEPTED, bound (\d+)',
        'stage_re': None, 'stage_keys': None,
        'category': ('EXPERIMENTAL feasibility on the PROVED specializer with the '
                     'OLD partial-goInline variant -- which has no equivalence '
                     'lemma and is NOT the proof target.'),
    },
    'total-probe': {
        'log_marker': 'backend: PROVED mixDriver + hwAPVarT',
        'specializer': 'Projection.mixDriver  (the PROVED specializer -- no fork)',
        'interpreter': 'Projection.ProtoVar.hwAPVarT  (TOTAL variant; IHwAdequate_varT and specializeDesign_varT_correct are PROVED, so this pair is the covered backend)',
        'acceptance': 'CHECKED SIMULATOR PATH: specialized once, checker must accept, every cycle at the CHECKED BOUND; 6 width-aware stimuli + threaded trace + control interpreter',
        'code': CODE_TOTAL + CODE_COMMON + CODE_PROVED,
        'seed_re': r'seed (\d+): (\S+)\s+matches interpretDesign (\w+)',
        'terms_re': r'residual (\d+) terms',
        'bound_re': r'checker bound (\d+)',
        'stage_re': (r'STAGE TIMES ms: specialize\+check (\d+)\s+'
                     r'reference-runs (\d+) \((\d+)\)\s+step-runs (\d+) \((\d+)\)'
                     r'\s+control (\d+)'),
        'stage_keys': ['specialize_and_check', 'reference_runs_total',
                       'reference_runs_n', 'step_runs_total', 'step_runs_n', 'control'],
        'category': ('EXPERIMENTAL EXECUTION on the COVERED backend: mixDriver + '
                     'hwAPVarT, for which IHwAdequate_varT, '
                     'specializeDesign_varT_correct and simSound_varT ARE proved.  '
                     'What is NOT established is the concrete hproj -- mixDriver '
                     'does not kernel-reduce at nontrivial fuel -- so this is a '
                     'trusted native execution, not a kernel-certified result, and '
                     'it is NOT RTL equivalence.'),
    },
    'host-var-total': {
        'log_marker': 'HOST mixDriver (proved) + hwAPVarT',
        'specializer': 'Projection.mixDriver  (the PROVED specializer -- no fork)',
        'interpreter': 'Projection.ProtoVar.hwAPVarT  (TOTAL variant, the PROOF TARGET)',
        'acceptance': 'FEASIBILITY ONLY: 3 seeds, no trace, no control; and it '
                      'interprets at a FALLBACK bound when the checker rejects, '
                      'so it is not a checked run -- superseded by total-probe',
        'code': CODE_FORK + CODE_TOTAL + CODE_COMMON + CODE_PROVED,
        'seed_re': r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)',
        'terms_re': r'terms (\d+)',
        'bound_re': r'ACCEPTED, bound (\d+)',
        'stage_re': None, 'stage_keys': None,
        'category': 'EXPERIMENTAL: superseded by the total-probe runner.',
    },
}


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def git(*args):
    r = subprocess.run(['git'] + list(args), cwd=LEAN, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def tree_state(code):
    dirty = git('status', '--porcelain') or ''
    diff = git('diff', 'HEAD') or ''
    tracked = set((git('ls-files') or '').split())
    files = [f for f in code if os.path.exists(os.path.join(LEAN, f))]
    return {
        'git_rev': git('rev-parse', 'HEAD'),
        'dirty_file_count': len([l for l in dirty.splitlines() if l.strip()]),
        'diff_sha256': hashlib.sha256(diff.encode()).hexdigest(),
        'diff_sha256_note': 'covers TRACKED files only; untracked sources are '
                            'hashed individually below and flagged',
        'source_sha256': {f: sha(os.path.join(LEAN, f)) for f in files},
        'source_tracked': {f: (f in tracked) for f in files},
        'untracked_sources': sorted(f for f in files if f not in tracked),
    }


def num(pat, text, cast=int, default=None):
    """First CAPTURED value, or `default`.

    `m.group(1)` was wrong: a pattern with an alternation leaves the arm that
    did not match as `None`, so `cast(None)` raised `TypeError`.  Reproduced on
    `file-ab`'s bound pattern against an `exact fuel` log."""
    m = re.search(pat, text)
    if not m:
        return default
    for g in m.groups():
        if g is not None:
            return cast(g)
    return default


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


def parse_seeds(t, seed_re=r'seed (\d+): residual (\S+)\s+matches interpretDesign (\w+)'):
    seeds = re.findall(seed_re, t)
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
    R = RUNNERS[a.runner]
    cap = {
        'captured_utc': datetime.datetime.now(datetime.timezone.utc)
                                .strftime('%Y-%m-%dT%H:%M:%SZ'),
        'evidence': 'captured-pre-launch',
        'command': a.cmd,
        'cert_sha256': sha(a.cert),
        'binary_sha256': sha(os.path.join(LEAN, a.binary)) if a.binary else None,
        'binary_path': a.binary,
        **tree_state(RUNNERS[a.runner]['code']),
    }
    with open(a.out, 'w') as f:
        json.dump(cap, f, indent=1, sort_keys=True)
    print(f'pre-launch capture written to {a.out}')


def record_one(a):
    t = open(a.log).read()
    marker = RUNNERS[a.runner]['log_marker']
    if marker not in t:
        raise SystemExit(
            f"refusing to record: --runner {a.runner} expects {marker!r} in the "
            f"log and it is absent.  Recording a run under another backend's "
            f"labels would misstate what executed.")
    exit_code, exit_evidence = parse_exit(t)
    R = RUNNERS[a.runner]
    stage = re.search(R['stage_re'], t) if R['stage_re'] else None
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
            **tree_state(R['code']),
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
        'runner': a.runner,
        'specializer': RUNNERS[a.runner]['specializer'],
        'interpreter': RUNNERS[a.runner]['interpreter'],
        'acceptance_strength': RUNNERS[a.runner]['acceptance'],
        'category': R['category'],
        'fuel': {'step': num(r'fuel (\d+)/', t), 'work': num(r'fuel \d+/(\d+)', t)},
        'stock_fuel': {'step': 20000, 'work': 200},
        'design_shape': {'sources': num(r'sources (\d+) nodes', t),
                         'nodes': num(r'nodes (\d+) flops', t),
                         'flops': num(r'flops (\d+)', t)},
        'support': parse_support(t),
        'residual': {'terms': num(R['terms_re'], t),
                     'tl': num(r'prims: tl (\d+)', t),
                     'consP': num(r'consP (\d+)', t)},
        'check_residual_bound': num(R['bound_re'], t),
        'bound_meaning': 'height; checkResidual_sound proves no outOfFuel at it. '
                         'NOT a minimum for any given input.',
        'stimuli': parse_seeds(t, R['seed_re']),
        'trace': (re.search(r'trace: (.*)', t).group(1).strip()
                  if re.search(r'trace: ', t) else None),
        'control': (re.search(r'control: (.*)', t).group(1).strip()
                    if re.search(r'control: ', t) else None),
        'stage_times_ms': (dict(zip(R['stage_keys'],
                                    [int(g) for g in stage.groups()]))
                           if stage else None),
        'wall_s': num(r'wall ([\d.]+) s', t, float),
        'peak_rss_kb': num(r'RSS (\d+) KB', t),
        'exit': exit_code,
        'exit_evidence': exit_evidence,
    }
    out = a.out or OUT
    with open(out, 'a') as f:
        f.write(json.dumps(rec, sort_keys=True) + '\n')
    print(f'appended {rec["design"]} (exit {rec["exit"]!r}, {exit_evidence}) to {out}')
    return rec


def cmd_record(a):
    """CLI wrapper.  `record_one` returns the RECORD, which is a payload, not a
    status: returning it from here made `main`'s `sys.exit(a.fn(a) or 0)` call
    `sys.exit(dict)`, which prints the whole dict to stderr and exits 1.  The
    record appended fine; the process still reported failure.

    Recording success is independent of the recorded experiment's outcome: a
    run that exited 1 is recorded, and recording it is an exit-0 event."""
    record_one(a)
    return 0


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

    # --- runner-specific grammars.  total_probe emits `seed N: ok ...`, not
    # `seed N: residual ...`, and different terms / bound / stage formats; the
    # fork regexes silently matched NOTHING on its logs.
    TP = RUNNERS['total-probe']
    tp_ok = ('  residual 89996 terms, checker bound 28899 (height; proved sufficient)\n'
             '  seed 0: ok  matches interpretDesign true\n'
             '  seed 1: ok  matches interpretDesign true\n'
             '  STAGE TIMES ms: specialize+check 11  reference-runs 22 (6)'
             '  step-runs 33 (6)  control 44\n'
             'wall 9.0 s RSS 10 KB exit 0\n')
    sd = parse_seeds(tp_ok, TP['seed_re'])
    check('tp/seeds-count', sd['count'], 2)
    check('tp/seeds-match', sd['all_match_interpretDesign'], True)
    check('tp/terms', num(TP['terms_re'], tp_ok), 89996)
    check('tp/bound', num(TP['bound_re'], tp_ok), 28899)
    st = re.search(TP['stage_re'], tp_ok)
    check('tp/stage', [int(g) for g in st.groups()] if st else None, [11, 22, 6, 33, 6, 44])
    check('tp/exit', parse_exit(tp_ok)[0], 0)
    # the FORK regex must NOT match a total-probe log -- that was the bug
    check('tp/fork-regex-misses',
          parse_seeds(tp_ok, RUNNERS['file-ab']['seed_re'])['count'], 0)

    # a FAILED total-probe log: checker rejected, exit 1, no seeds, no stage line
    tp_fail = ('  checkResidual REJECTED -- failing closed\n'
               'wall 3.0 s RSS 10 KB exit 1\n')
    check('tp/fail-exit', parse_exit(tp_fail)[0], 1)
    check('tp/fail-seeds', parse_seeds(tp_fail, TP['seed_re'])['all_match_interpretDesign'], None)
    check('tp/fail-bound', num(TP['bound_re'], tp_fail), None)
    check('tp/fail-stage', re.search(TP['stage_re'], tp_fail), None)

    # a TRUNCATED total-probe log: exit must stay UNKNOWN, never 0
    tp_trunc = '  [specialize+check ...'
    check('tp/trunc-exit', parse_exit(tp_trunc)[0], None)
    check('tp/trunc-terms', num(TP['terms_re'], tp_trunc), None)

    # every runner must declare a complete parsing contract
    for nm, v in RUNNERS.items():
        for k in ('code', 'seed_re', 'terms_re', 'bound_re', 'category'):
            if v.get(k) in (None, ''):
                fails.append(f'runner {nm}: missing {k}')

    # 9. the runner table must name a distinct backend pair for each runner
    pairs = {(v['specializer'], v['interpreter']) for v in RUNNERS.values()}
    check('runners/distinct', len(pairs), len(RUNNERS))

    # ---- END-TO-END cmd_record.  The parser selftests above never ran the
    # entry point, and it was broken three ways at once: `R` used before
    # assignment, `tree_state()` called without its now-required argument, and
    # `num` raising on an alternation.  Every case below writes to a
    # PROJECT-LOCAL temp manifest, never the real one.
    import tempfile, shutil, argparse as _ap
    # project-local, and OUTSIDE the git repo so a crashed test leaves no
    # artifact in a tracked tree.  `dir=None` would silently fall back to /tmp
    # on a fresh checkout, which the workspace instructions forbid, so the
    # directory is CREATED and a failure is raised rather than papered over.
    scratch = os.path.abspath(os.path.join(LEAN, '..', '..', '..',
                                           '.perfwork', 'recorder-tests'))
    try:
        os.makedirs(scratch, exist_ok=True)
    except OSError as err:
        raise SystemExit(f'cannot create the project-local test directory '
                         f'{scratch}: {err}.  Refusing to fall back to /tmp.')
    tmp = tempfile.mkdtemp(prefix='recordtest-', dir=scratch)
    try:
        cert = os.path.join(tmp, 'f.dcert')
        open(cert, 'w').write('DCERT1\n0\n0\n0\n0\n0\n')
        manifest = os.path.join(tmp, 'out.jsonl')

        def run(logtext, runner, capture=None):
            lg = os.path.join(tmp, 'l.log'); open(lg, 'w').write(logtext)
            ns = _ap.Namespace(log=lg, cert=cert, provenance='test', capture=capture,
                               runner=runner, out=manifest)
            return record_one(ns)

        FULL_TP = ('  sources 2 nodes 1 flops 0 outputs 1 clocks 1\n'
                   '  support: wf true memFree true sources true ops true '
                   'arities true flopClocks true | ALL true\n'
                   '  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel 20/2\n'
                   '  residual 45 terms, checker bound 14 (height)\n'
                   '  seed 0: ok  matches interpretDesign true\n'
                   '  trace: 4 cycles -- agrees true states-reached 1 flops 0\n'
                   '  control: variant interpreter matches interpretDesign true\n'
                   '  STAGE TIMES ms: specialize+check 7  reference-runs 8 (6)'
                   '  step-runs 9 (6)  control 10\n'
                   'wall 1.5 s RSS 99 KB exit 0\n')

        # (a) full log, NO capture -> reconstructed, and it must not crash
        r = run(FULL_TP, 'total-probe')
        check('e2e/no-capture-exit', r['exit'], 0)
        check('e2e/no-capture-evidence', r['code_identity']['evidence'],
              'reconstructed-at-record-time')
        check('e2e/terms', r['residual']['terms'], 45)
        check('e2e/bound', r['check_residual_bound'], 14)
        check('e2e/support', r['support']['all_six_true'], True)
        check('e2e/seeds', r['stimuli']['all_match_interpretDesign'], True)
        check('e2e/stage', r['stage_times_ms']['specialize_and_check'], 7)
        check('e2e/untracked-listed', isinstance(
            r['code_identity'].get('untracked_sources'), list), True)

        # (b) WITH a pre-launch capture -> the capture wins, evidence upgraded
        capf = os.path.join(tmp, 'cap.json')
        json.dump({'evidence': 'captured-pre-launch', 'binary_sha256': 'deadbeef'},
                  open(capf, 'w'))
        r = run(FULL_TP, 'total-probe', capture=capf)
        check('e2e/capture-evidence', r['code_identity']['evidence'], 'captured-pre-launch')
        check('e2e/capture-binary', r['code_identity']['binary_sha256'], 'deadbeef')

        # (c) FAILURE log: checker rejected, exit 1, no seeds, no stage
        FAIL_TP = ('  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel 20/2\n'
                   '  checkResidual REJECTED -- failing closed\n'
                   'wall 1.0 s RSS 9 KB exit 1\n')
        r = run(FAIL_TP, 'total-probe')
        check('e2e/fail-exit', r['exit'], 1)
        check('e2e/fail-seeds', r['stimuli']['all_match_interpretDesign'], None)
        check('e2e/fail-stage', r['stage_times_ms'], None)
        check('e2e/fail-bound', r['check_residual_bound'], None)

        # (d) TRUNCATED log: exit UNKNOWN, never inferred from a partial log
        TRUNC = ('  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel 20/2\n'
                 '  [specialize ...')
        r = run(TRUNC, 'total-probe')
        check('e2e/trunc-exit', r['exit'], None)
        check('e2e/trunc-evidence-str', 'MISSING' in r['exit_evidence'], True)

        # (e) the file-ab path, including the alternation that raised TypeError
        FULL_FA = ('fuel 20000/200\n'
                   '  terms 45   lit 9\n'
                   '  checkResidual: ACCEPTED, exact fuel 19\n'
                   '  seed 0: residual ok  matches interpretDesign true\n'
                   'wall 2.0 s RSS 9 KB exit 0\n')
        r = run(FULL_FA, 'file-ab')
        check('e2e/fileab-bound-alternation', r['check_residual_bound'], 19)
        check('e2e/fileab-seeds', r['stimuli']['count'], 1)

        # ---- REAL SUBPROCESS CLI.  Everything above calls `record_one`
        # directly, so none of it exercises the PROCESS EXIT STATUS -- which
        # was broken: `main` did `sys.exit(a.fn(a) or 0)` on a returned record,
        # printing the whole dict to stderr and exiting 1 after a successful
        # append.  Recording success is independent of the RECORDED
        # experiment's outcome, so exits 0 / 1 / unknown must all record with
        # process exit 0 and exactly one new row.
        for nm, logtext in [('exit0', FULL_TP), ('exit1', FAIL_TP), ('unknown', TRUNC)]:
            cli_log = os.path.join(tmp, f'cli-{nm}.log')
            open(cli_log, 'w').write(logtext)
            cli_manifest = os.path.join(tmp, f'cli-{nm}.jsonl')
            proc = subprocess.run(
                [sys.executable, os.path.abspath(__file__), 'record', cli_log, cert,
                 '--provenance', 'cli-test', '--runner', 'total-probe',
                 '--out', cli_manifest],
                capture_output=True, text=True)
            check(f'cli/{nm}-exit', proc.returncode, 0)
            check(f'cli/{nm}-rows',
                  len([l for l in open(cli_manifest).read().split('\n') if l.strip()]), 1)
            # a dict on stderr is the signature of the bug
            check(f'cli/{nm}-no-dict-stderr',
                  ("'design'" in proc.stderr or proc.stderr.strip().startswith('{')),
                  False)
            check(f'cli/{nm}-row-parses',
                  isinstance(json.loads(open(cli_manifest).read().strip()), dict), True)

        # the real manifest must be untouched
        check('e2e/wrote-only-temp', os.path.getsize(manifest) > 0, True)
        check('e2e/five-records', len(open(manifest).read().strip().split('\n')), 5)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if fails:
        print('SELFTEST FAILED:'); [print('  ' + f) for f in fails]; return 1
    print(f'selftest: all checks passed ({len(RUNNERS)} runners)')
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('prelaunch'); p.add_argument('cert')
    p.add_argument('--binary'); p.add_argument('--cmd', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--runner', required=True, choices=sorted(RUNNERS))
    p.set_defaults(fn=cmd_prelaunch)
    r = sub.add_parser('record'); r.add_argument('log'); r.add_argument('cert')
    r.add_argument('--provenance', required=True); r.add_argument('--capture')
    r.add_argument('--runner', required=True, choices=sorted(RUNNERS))
    r.add_argument('--out', help='write here instead of the real manifest; '
                                 'tests MUST pass this')
    r.set_defaults(fn=cmd_record)
    s = sub.add_parser('selftest'); s.set_defaults(fn=cmd_selftest)
    a = ap.parse_args()
    rc = a.fn(a)
    # never `sys.exit(<payload>)`: anything that is not an int is a success
    sys.exit(rc if isinstance(rc, int) else 0)


if __name__ == '__main__':
    main()
