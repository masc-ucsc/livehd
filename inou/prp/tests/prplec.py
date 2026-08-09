#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prplec — the `:type: lec` test owner: PYROPE-vs-PYROPE equivalence.
#
# `tests/equiv/foo.prp` + `foo.v` is the Pyrope-vs-Verilog axis (prplib.run_equiv).
# This module is the OTHER axis: two Pyrope spellings of one design, proven
# equivalent by `lhd lec`. There is no per-test script and no per-test BUILD
# rule — the group is discovered from the FILE NAME:
#
#     foo.prp      the reference
#     foo_1.prp    a variant: LEC'd against foo.prp
#     foo_2.prp    another variant, also against foo.prp (never against foo_1)
#
# so a new claim about `foo` is one new file. A variant that is HEADER-ONLY (no
# Pyrope code, just the `/* … */` tags) means "the same source as the base, with
# MY flags": that is how a rolled-vs-unrolled or any other compile-flag
# differential is written without duplicating the design.
#
# Header tags (all optional; `:lec_*:` tags may repeat where noted):
#
#   :lec_top:      top entity on BOTH sides (default `top`)
#   :set: k=v …    COMPILE flags for that side. A variant with no `:set:` of its
#                  own inherits the base's, so the two sides differ only when a
#                  variant says so. Two sides with the same flags are LEC'd
#                  directly from source (`lhd lec --ref a.prp --impl b.prp`);
#                  different flags make the harness compile each side to its own
#                  `lg:` library first (one `--set` cannot say two things).
#   :lec_set: k=v … extra `--set` for `lhd lec` (base = shared, variant = adds)
#   :lec_sweep: k=v1,v2   run the pair once per value (repeatable: cartesian)
#   :lec_expect: proven|refuted   what the run must report (default proven)
#   :lec_grep: REGEX      must appear in the lec output (repeatable)
#   :lec_grep_not: REGEX  must NOT appear in the lec output (repeatable)
#
# Run one group by hand (base = every variant, or name a single variant):
#
#     ./inou/prp/tests/prplec.py inou/prp/tests/equiv/loop_lec_index.prp
#     ./inou/prp/tests/prplec.py inou/prp/tests/equiv/loop_lec_index_1.prp -v

import glob as globmod
import itertools
import os
import re
import shutil
import subprocess
import sys

# `<base>_<N>.prp`. Greedy stem so `a_b_2` groups under `a_b`, matching the
# BUILD-side `rsplit("_", 1)`.
_VARIANT_RE = re.compile(r'^(.+)_(\d+)$')

# Verdict lines of `lhd lec`. PASS(n) is a completed bounded proof, so it counts
# as proven exactly as run_equiv treats it.
_PROVEN_RE  = re.compile(r'(?m)^lec: .* (?:PROVEN|PASS\(\d+\)) equivalent')
_REFUTED_RE = re.compile(r'(?m)^lec: .* REFUTED')


def variant_of(prp_file):
    """(base_path, ordinal) if `prp_file` is a `<base>_<N>.prp`, else None.

    Purely lexical — the caller decides whether the base actually exists.
    """
    head, tail = os.path.split(prp_file)
    if not tail.endswith('.prp'):
        return None
    m = _VARIANT_RE.match(tail[:-len('.prp')])
    if not m:
        return None
    return os.path.join(head, m.group(1) + '.prp'), int(m.group(2))


def group_of(prp_file, root=''):
    """(base, [variants]) for a fixture that is either the base or a variant.

    Naming a VARIANT selects just that one (this is what each `prp-lec-*` BUILD
    target does — one target per claim). Naming the BASE selects every variant
    staged beside it, which is what a manual run wants.
    """
    join = lambda p: p if os.path.isabs(p) or not root else os.path.join(root, p)

    v = variant_of(prp_file)
    if v and os.path.exists(join(v[0])):
        return v[0], [prp_file]

    stem, found = prp_file[:-len('.prp')], []
    for hit in globmod.glob(join(stem) + '_*.prp'):
        cand = os.path.join(os.path.dirname(prp_file), os.path.basename(hit))
        vv = variant_of(cand)
        if vv and vv[0] == prp_file:
            found.append((vv[1], cand))
    return prp_file, [c for _, c in sorted(found)]


def is_header_only(path):
    """True when the file holds no Pyrope code — only `/* … */` / `//` comments.

    Such a variant reuses the BASE source with its own `:set:` flags, so a
    compile-flag differential (rolled vs unrolled, one reset style vs another)
    never has to duplicate the design.
    """
    try:
        with open(path) as f:
            text = f.read()
    except OSError:
        return False
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text.strip() == ''


def _tags(test, name):
    """Every occurrence of a repeatable `:name:` header tag, in file order."""
    return [v for v in test.multi.get(name, []) if v.strip()]


def _sets(test, name='set'):
    """`:set: a=1 b=2` -> ['a=1', 'b=2'] (space separated, as everywhere else)."""
    return (test.params.get(name) or '').split()


def _merge_sets(*groups):
    """Later `k=v` wins over an earlier one for the same k; order is preserved."""
    out = {}
    for g in groups:
        for tok in g:
            out[tok.split('=')[0]] = tok
    return list(out.values())


def _set_args(sets):
    return [a for tok in sets for a in ('--set', tok)]


def _run(cmd, cwd):
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        log, _ = proc.communicate()
        return proc.returncode, log.decode('utf-8', 'ignore')
    except BaseException as e:
        proc.kill()
        return 1, 'exception while running {}: {!r}\n'.format(cmd[0], e)


def _tail(text, lines=25):
    return '\n'.join(text.splitlines()[-lines:])


class _Side:
    """One half of the pair: which source to elaborate, with which flags."""

    def __init__(self, label, src, sets):
        self.label = label
        self.src   = src
        self.sets  = sets
        self.arg   = 'pyrope:' + src   # replaced by `lg:<dir>` when pre-compiled


def _precompile(runner, tmp_dir, side, top, odir):
    """Elaborate one side into its own `lg:` library (needed only when the two
    sides disagree on COMPILE flags — a single `lhd lec --set` cannot)."""
    cmd = [runner.lhd, 'compile', side.src, '--top', top,
           '--emit-dir', 'lg:' + odir, '--workdir', odir + '_w'] + _set_args(side.sets)
    rc, log = _run(cmd, tmp_dir)
    if rc != 0:
        return 'compiling the {} side failed (rc={})\n  {}\n{}'.format(
            side.label, rc, ' '.join(cmd), _tail(log))
    side.arg = 'lg:' + odir
    return None


def _sweeps(base_test, var_test):
    """`:lec_sweep: k=v1,v2` -> the list of `--set` combinations to run.

    Several sweep tags multiply out; none means a single run with no extra set.
    """
    axes = []
    for spec in _tags(base_test, 'lec_sweep') + _tags(var_test, 'lec_sweep'):
        key, _, values = spec.partition('=')
        axes.append(['{}={}'.format(key.strip(), v.strip()) for v in values.split(',') if v.strip()])
    return [list(c) for c in itertools.product(*axes)] if axes else [[]]


def _check(out, rc, expect, base_test, var_test):
    """The verdict + `:lec_grep:` contract for one run. Returns a reason or None."""
    if expect == 'refuted':
        if rc == 0 or not _REFUTED_RE.search(out):
            return 'expected REFUTED, got rc={} (a passing lec means the two sides agree)'.format(rc)
    elif expect == 'proven':
        if rc != 0 or not _PROVEN_RE.search(out):
            return 'expected PROVEN, got rc={}'.format(rc)
    else:
        return ':lec_expect: {} is not one of proven|refuted'.format(expect)

    for pat in _tags(base_test, 'lec_grep') + _tags(var_test, 'lec_grep'):
        if not re.search(pat, out):
            return 'the lec output never matched :lec_grep: {}'.format(pat)
    for pat in _tags(base_test, 'lec_grep_not') + _tags(var_test, 'lec_grep_not'):
        if re.search(pat, out):
            return 'the lec output matched :lec_grep_not: {}'.format(pat)
    return None


def _run_pair(runner, tmp_dir, base_test, var_test, verbose=False):
    """LEC one variant against its base, once per `:lec_sweep:` combination."""
    from prplib import PrpRunner

    base, var = base_test.params['files'][0], var_test.params['files'][0]
    name = os.path.basename(var)
    top  = (var_test.params.get('lec_top') or base_test.params.get('lec_top') or 'top').strip()

    # One scratch tree per pair, wiped up front: an `--emit-dir` that still holds
    # a previous run's library is how a stale definition sneaks into a proof.
    # `tmp*` keeps it under the repo .gitignore for manual runs from the root.
    scratch = os.path.join(tmp_dir, 'tmp_lec_' + PrpRunner._safe_name(var_test))
    shutil.rmtree(scratch, ignore_errors=True)

    # A header-only variant means "the base source, my flags"; anything else is
    # its own design.
    var_src = base if is_header_only(os.path.join(tmp_dir, var) if not os.path.isabs(var) else var) else var

    ref = _Side('ref', base, _sets(base_test))
    # A variant that declares no `:set:` of its own inherits the base's, so the
    # two sides differ in exactly the way the variant says they do.
    impl = _Side('impl', var_src, _sets(var_test) if var_test.params.get('set') else ref.sets)

    # Same flags on both sides -> hand the .prp files straight to `lhd lec` and
    # let its own elaboration carry those flags (this also exercises lec's
    # pyrope: input path). Same flags AND the same source is the SELF check: one
    # design elaborated twice, a real claim about the engine.
    shared_compile = ref.sets
    if impl.sets != ref.sets:
        # Different compile flags per side: one `lhd lec --set` cannot say two
        # things, so each side is compiled into its own library first.
        shared_compile = []
        for side in (ref, impl):
            err = _precompile(runner, tmp_dir, side, top, os.path.join(scratch, side.label))
            if err:
                print('{} - lec - FAILED: {}'.format(name, err))
                return 1

    expect = (var_test.params.get('lec_expect') or 'proven').strip().lower()
    lec_base = _merge_sets(['formal.cache=false'],   # hermetic: no cross-run reuse
                           shared_compile,
                           _sets(base_test, 'lec_set'), _sets(var_test, 'lec_set'))

    rc_all = 0
    for combo in _sweeps(base_test, var_test):
        sets = _merge_sets(lec_base, combo)
        wd   = os.path.join(scratch, 'w_' + (re.sub(r'\W+', '_', '_'.join(combo)) or 'default'))
        cmd = [runner.lhd, 'lec', '--ref', ref.arg, '--impl', impl.arg,
               '--ref-top', top, '--impl-top', top, '--workdir', wd] + _set_args(sets)
        rc, out = _run(cmd, tmp_dir)
        if verbose:
            print(out)

        why  = _check(out, rc, expect, base_test, var_test)
        what = '{} vs {}{}'.format(os.path.basename(base), name,
                                   ' [' + ' '.join(combo) + ']' if combo else '')
        if why:
            print('{} - lec - FAILED: {}: {}'.format(name, what, why))
            print('  ' + ' '.join(cmd))
            if not verbose:
                print(_tail(out))
            rc_all = 1
        else:
            print('{} - lec - success ({}: {})'.format(name, what, expect))
    return rc_all


def run_prplec(runner, tmp_dir, test, verbose=False):
    """`:type: lec` entry point: LEC a group's base against its variant(s)."""
    from prplib import PrpTest

    given = test.params['files'][0]
    base, variants = group_of(given, tmp_dir)
    abs_of = lambda p: p if os.path.isabs(p) else os.path.join(tmp_dir, p)
    shown = os.path.basename(given)

    if not variants:
        orphan = variant_of(given)
        if orphan:
            print('{} - lec - FAILED: names a `_<N>` variant, but its base {} is not beside '
                  'it'.format(shown, os.path.basename(orphan[0])))
        else:
            print('{} - lec - FAILED: no `{}_<N>.prp` beside it (a Pyrope-vs-Pyrope group is a '
                  'base plus at least one numbered sibling)'.format(shown, shown[:-len('.prp')]))
        return 1

    # The base fixture carries the group-wide defaults; when the target names the
    # base itself, `test` already IS that fixture.
    base_test = test if base == given else PrpTest(abs_of(base))
    base_test.params['files'] = [base]

    rc = 0
    for var in variants:
        var_test = test if var == given else PrpTest(abs_of(var))
        var_test.params['files'] = [var]
        rc |= _run_pair(runner, tmp_dir, base_test, var_test, verbose)
    return rc


if __name__ == '__main__':
    import argparse

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from prplib import PrpRunner, PrpTest

    ap = argparse.ArgumentParser(
        description='LEC a Pyrope fixture against its `_<N>` variants (run from the repo root)')
    ap.add_argument('fixtures', nargs='+', help='tests/equiv/foo.prp (all variants) or tests/equiv/foo_1.prp')
    ap.add_argument('-v', '--verbose', action='store_true', help='print the full `lhd lec` output')
    opts = ap.parse_args()

    runner, rc = PrpRunner(), 0
    for fixture in opts.fixtures:
        rc |= run_prplec(runner, os.getcwd(), PrpTest(fixture), opts.verbose)
    sys.exit(1 if rc else 0)
