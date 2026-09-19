#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# sim.tune DRIVER contract (sim_profile.md §6; prp_sim_rt.hpp): what the generated
# drv.bin promises on its own, independent of the lhd-side tuner.
#
#   * `--set sim.unknown_zero=true` at RUN time is byte-identical (stdout, end and
#     out digests) to a build GENERATED with sim.unknown_zero=true, and
#     `--set sim.unknown_zero=false` on such a build is refused (exit 2);
#   * every --result-json row carries the run metrics and the two digests, and
#     sim_cycles counts exactly the DUT steps the test executed;
#   * profiling changes no simulated value (digests equal on vs off), writes a
#     raw run file, and under a fixed stride its activity counts are
#     reproducible run to run;
#   * the idleness is reported under every support-class weight
#     (`profile.weights` ge / sites / cost / cost_flat, 0 <= idle <= pairs*total,
#     ge == the `support` object), occurrences carry their `cost`, and the raw
#     file ALONE carries the per-class calibration dump `profile.class_dump`
#     (one idle-pair count per class of the root's support table);
#   * the codegen-key check: restating the baked value is accepted, a different
#     one is refused, build plumbing is ignored, a setup-only key is refused;
#   * `set` is a reserved test-parameter name (it would shadow `--set`).
#
# Hermetic like the prp-sim targets: `lhd sim --setup-only`, then the host C++
# compiler over the generated sources with the runfiles' hlop/iassert headers.
# NOTE: runs under /usr/bin/python3 (3.9) inside the bazel sandbox -- keep it
# 3.9-clean (no `X | None`, no match).

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prpsim  # noqa: E402  (_sim_compiler / _sim_include_dirs: the prp-sim harness's hermetic build)

FAILS = []


def check(cond, what):
    if cond:
        print('ok   ' + what)
    else:
        print('FAIL ' + what)
        FAILS.append(what)


def find_lhd():
    for p in ('./bazel-bin/lhd/lhd', './lhd/lhd'):
        if os.path.exists(p):
            return os.path.abspath(p)
    sys.exit('cannot find the lhd binary')


def setup(lhd, prp, work, sets):
    cmd = [lhd, 'sim', prp, '--setup-only', '--workdir', work, '-q', '--set', 'sim.tune.profile=off'] + sets
    cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return cp.returncode, cp.stdout.decode('utf-8', 'ignore')


def build(simdir, incs):
    # Every generated source the driver needs: each included unit's body, its
    # color kernels, and a color root's sim.tune TUs (support table + identity).
    drv = os.path.join(simdir, 'drv.cpp')
    srcs = [drv]
    for fn in sorted(os.listdir(simdir)):
        if fn.endswith('.cpp') and fn != 'drv.cpp':
            srcs.append(os.path.join(simdir, fn))
    exe = os.path.join(simdir, 'drv.bin')
    cc = [prpsim._sim_compiler(), '-std=c++23', '-DNDEBUG', '-O1', '-pthread', '-I' + simdir]
    cc += ['-I' + d for d in incs]
    cp = subprocess.run(cc + srcs + ['-o', exe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if cp.returncode != 0:
        print(cp.stdout.decode('utf-8', 'ignore'))
        sys.exit('driver build failed in ' + simdir)
    return exe


def run(exe, args, rj=None):
    cmd = [exe, '--no-checkpoint'] + args
    if rj:
        cmd += ['--result-json', rj]
    cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return cp.returncode, cp.stdout.decode('utf-8', 'ignore'), cp.stderr.decode('utf-8', 'ignore')


def row(rj):
    with open(rj) as f:
        rows = json.load(f)
    return rows[0]


WEIGHTS = ('ge', 'sites', 'cost', 'cost_flat')


def support_classes(simdir):
    # The color root's class count, from its sim.tune TU's summary comment.
    for fn in sorted(os.listdir(simdir)):
        if fn.endswith('.tune.cpp'):
            with open(os.path.join(simdir, fn)) as f:
                m = re.search(r'^// words=\d+ sources=\d+ exact=\w+ classes=(\d+) ', f.read(), re.M)
            if m:
                return int(m.group(1))
    return None


def check_weights(prof, what, all_idle=False):
    # profile.weights: every weight present, idle within [0, pairs * total], the
    # ge weight equal to the support object's, every cost at least one word per
    # site. `all_idle`: nothing moved, so idle == pairs * total for each.
    w = prof.get('weights') or {}
    pairs = prof.get('pairs', 0)
    check(sorted(w.keys()) == sorted(WEIGHTS), '%s: profile.weights has %s (%s)' % (what, '/'.join(WEIGHTS), sorted(w.keys())))
    ok = all(isinstance(w.get(k, {}).get('total'), int) and isinstance(w.get(k, {}).get('idle'), int) for k in WEIGHTS)
    check(ok, '%s: every weight has integer total + idle' % what)
    if not ok:
        return
    check(all(0 <= w[k]['idle'] <= pairs * w[k]['total'] for k in WEIGHTS),
          '%s: 0 <= idle <= pairs * total for every weight (%s)' % (what, json.dumps(w)))
    sup = prof.get('support') or {}
    check(w['ge']['total'] == sup.get('total_ge') and w['ge']['idle'] == sup.get('idle_ge'),
          '%s: weights.ge == the support object' % what)
    check(w['sites']['total'] > 0 and w['cost']['total'] >= w['sites']['total']
          and w['cost_flat']['total'] >= w['sites']['total'],
          '%s: every executable site is classed and costs >= 1 word' % what)
    if all_idle:
        check(all(w[k]['idle'] == pairs * w[k]['total'] for k in WEIGHTS), '%s: nothing moved -> fully idle' % what)
    occ = prof.get('occ', [])
    check(len(occ) > 0 and all(isinstance(o.get('cost'), int) and o['cost'] >= 1 for o in occ),
          '%s: every occurrence entry carries its cost' % what)


def check_class_dump(prof, classes, roots, what, all_idle=False):
    dump = prof.get('class_dump')
    check(isinstance(dump, list) and len(dump) == 1, '%s: raw file has one class_dump entry per DUT var' % what)
    if not isinstance(dump, list) or len(dump) != 1:
        return
    d = dump[0]
    idle = d.get('idle_pairs') or []
    pairs = prof.get('pairs', 0)
    check(d.get('var') == 'dut' and d.get('module') in roots,
          '%s: class_dump names var + a raw-file root module (%s/%s vs %s)' % (what, d.get('var'), d.get('module'), roots))
    check(classes is not None and len(idle) == classes and 0 < classes <= 4096,
          '%s: one idle-pair count per support class (%s vs %s)' % (what, len(idle), classes))
    check(all(isinstance(x, int) and 0 <= x <= pairs for x in idle), '%s: 0 <= idle_pairs <= pairs' % what)
    if all_idle:
        check(all(x == pairs for x in idle), '%s: nothing moved -> every class idle in every pair' % what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-i', '--input', required=True, help='the unknown_tb_literal_once fixture')
    args = ap.parse_args()
    prp = os.path.abspath(args.input)
    lhd = find_lhd()

    tmp = os.environ.get('TEST_TMPDIR') or tempfile.mkdtemp(prefix='sim_tune_drv_')
    incs = prpsim._sim_include_dirs(tmp)
    if not incs:
        print('SKIP: hlop/iassert headers not found (no runfiles)')
        return 0

    # ---- `set` is a reserved test-parameter name ----
    bad = os.path.join(tmp, 'set_param.prp')
    with open(bad, 'w') as f:
        f.write('mod pass1(a:u8) -> (r:u8@[0]) {\n  r = a\n}\n\n'
                'test pass1.t(set:u8 = 1) {\n  mut d = pass1\n  d.a = set\n  step\n  assert(d.r == set)\n}\n')
    rc, out = setup(lhd, bad, os.path.join(tmp, 'wbad'), [])
    check(rc != 0 and 'not a usable simulation parameter name' in out, 'a test parameter named `set` is refused at setup')

    # ---- two setups: run-time random fill vs baked sim.unknown_zero=true ----
    wr, wz = os.path.join(tmp, 'wr'), os.path.join(tmp, 'wz')
    rc, out = setup(lhd, prp, wr, [])
    if rc != 0:
        print(out)
        sys.exit('setup failed')
    rc, out = setup(lhd, prp, wz, ['--set', 'sim.unknown_zero=true'])
    if rc != 0:
        print(out)
        sys.exit('setup (unknown_zero=true) failed')
    with open(os.path.join(wr, 'sim', 'drv.cpp')) as f:
        drv_src = f.read()
    check('// driver-set-parser: 1' in drv_src and '// tune-sampler: 1' in drv_src, 'driver carries the sim.tune markers')
    check(re.search(r'__lhd_unknown_literal<65, \d+ull>\("0ub\?{64}"\)', drv_src) is not None,
          'a testbench `?` literal goes through the once-per-program helper')
    check('_tb_unknown_literals = 1;' in drv_src, 'the distinct testbench `?` literal count is baked (two sites, one literal)')
    exe_r = build(os.path.join(wr, 'sim'), incs)
    exe_z = build(os.path.join(wz, 'sim'), incs)
    classes = support_classes(os.path.join(wr, 'sim'))
    check(classes is not None, 'the color root has a sim.tune support TU')

    # ---- metrics + digests on every row ----
    rj = os.path.join(tmp, 'r_off.json')
    rc, out_r, _ = run(exe_r, [], rj)
    check(rc == 0, 'random fill: the self-checking test passes')
    r_off = row(rj)
    for k in ('sim_cycles', 'init_ns', 'sim_ns', 'cpu_ns', 'cpu_cycles', 'instructions', 'pcore_frac', 'counters',
              'rng_draws', 'ckpt_taken', 'end_digest', 'out_digest'):
        check(k in r_off, 'result row has `%s`' % k)
    check(r_off.get('sim_cycles') == 4096, 'sim_cycles counts the executed DUT steps (4096)')
    check(re.fullmatch(r'[0-9a-f]{16}', r_off.get('end_digest', '')) is not None, 'end_digest is 16 hex digits')
    check('profile' not in r_off, 'no profile object when profiling is off')

    # ---- run-time zero fill == generated zero fill ----
    rj_z1, rj_z2 = os.path.join(tmp, 'z_rt.json'), os.path.join(tmp, 'z_baked.json')
    rc1, out_z1, _ = run(exe_r, ['--set', 'sim.unknown_zero=true'], rj_z1)
    rc2, out_z2, _ = run(exe_z, [], rj_z2)
    check(rc1 == 0 and rc2 == 0, 'zero fill (run-time and baked) passes')
    check(out_z1 == out_z2, 'run-time `--set sim.unknown_zero=true` stdout == a sim.unknown_zero=true build')
    z1, z2 = row(rj_z1), row(rj_z2)
    check(z1['end_digest'] == z2['end_digest'] and z1['out_digest'] == z2['out_digest'],
          'run-time and baked zero fill give the same end/out digests')
    rc, _, err = run(exe_z, ['--set', 'sim.unknown_zero=false'])
    check(rc == 2 and 'sim.unknown_zero=true' in err, '`--set sim.unknown_zero=false` on a baked-zero build is refused')

    # ---- profiling: no simulated value changes; raw file; fixed-stride repro ----
    counts = []
    for i in (1, 2):
        pdir = os.path.join(tmp, 'prof%d' % i)
        rj_p = os.path.join(tmp, 'r_on%d.json' % i)
        rc, out_p, _ = run(exe_r, ['--set', 'sim.tune.profile=on', '--set', 'sim.tune.profile_stride=64',
                                   '--set=sim.tune.profile_dir=' + pdir], rj_p)
        check(rc == 0 and out_p == out_r, 'profiling run %d: same verdict and stdout' % i)
        p = row(rj_p)
        check(p['end_digest'] == r_off['end_digest'] and p['out_digest'] == r_off['out_digest'],
              'profiling run %d: digests equal the unprofiled run' % i)
        prof = p.get('profile') or {}
        check(prof.get('pairs', 0) > 0, 'profiling run %d samples pairs (%s)' % (i, prof.get('pairs')))
        check_weights(prof, 'profiling run %d' % i)
        check('class_dump' not in prof, 'profiling run %d: sim_tests.json rows carry no class_dump' % i)
        # `acc += a` with `a` a nonzero random draw: the accumulator's classes
        # are active, so no weight reads fully idle.
        wc = (prof.get('weights') or {}).get('cost') or {}
        check(wc.get('idle', 0) < prof.get('pairs', 0) * wc.get('total', 0),
              'profiling run %d: the moving accumulator is not idle under the cost weight' % i)
        counts.append((prof.get('pairs'), prof.get('quiescent_pairs'), json.dumps(prof.get('support')),
                       json.dumps(prof.get('walker')), json.dumps(prof.get('weights'), sort_keys=True),
                       json.dumps([o.get('active_pairs') for o in prof.get('occ', [])])))
        raws = glob.glob(os.path.join(pdir, '*.json'))
        check(len(raws) == 1, 'profiling run %d writes one raw run file' % i)
        if raws:
            with open(raws[0]) as f:
                raw = json.load(f)
            check(raw.get('schema') == 'lhd-sim-tune-raw-1', 'raw file schema')
            check(raw.get('fill') == 'random' and raw.get('tb_unknown_literals') == 1, 'raw file: fill + tb literal count')
            check(len(raw.get('roots', [])) == 1 and raw['roots'][0].get('var') == 'dut', 'raw file: one DUT root')
            check(raw.get('selected') == ['unknown_tb_literal_once.drawn_once'], 'raw file: ordered selection')
            rprof = raw['tests'][0].get('profile', {})
            check(rprof.get('pairs') == prof.get('pairs'), 'raw file row == result row')
            check(rprof.get('weights') == prof.get('weights'), 'raw file weights == result row weights')
            check_class_dump(rprof, classes, [r.get('module') for r in raw.get('roots', [])], 'profiling run %d' % i)
    check(counts[0] == counts[1], 'fixed stride: the activity counts reproduce exactly')

    # Zero fill: `a` is 0 every cycle, so the accumulator never moves -- every
    # sampled pair is quiescent, inputs included.
    rj_q = os.path.join(tmp, 'r_q.json')
    rc, _, _ = run(exe_r, ['--set', 'sim.unknown_zero=true', '--set', 'sim.tune.profile=on',
                           '--set', 'sim.tune.profile_stride=64', '--set', 'sim.tune.profile_dir=' + os.path.join(tmp, 'profq')],
                   rj_q)
    q = row(rj_q).get('profile') or {}
    check(rc == 0 and q.get('pairs', 0) > 0 and q.get('quiescent_pairs') == q.get('pairs'),
          'zero fill: every sampled pair is quiescent (%s/%s)' % (q.get('quiescent_pairs'), q.get('pairs')))
    check_weights(q, 'zero fill', all_idle=True)
    raws = glob.glob(os.path.join(tmp, 'profq', '*.json'))
    if raws:
        with open(raws[0]) as f:
            rawq = json.load(f)
        check_class_dump(rawq['tests'][0].get('profile', {}), classes, [r.get('module') for r in rawq.get('roots', [])],
                         'zero fill', all_idle=True)
    else:
        check(False, 'zero fill writes a raw run file')

    # ---- the `--set` key classes ----
    # the binary was generated with the defaults (dirty on): equal is accepted, different refused
    for kv, want in (('sim.tune.dirty=on', 0), ('sim.tune.dirty=auto', 0), ('sim.tune.dirty=off', 2),
                     ('sim.tune.live_words=0', 2), ('sim.jobs=8', 0), ('sim.tune.file=/nope', 0),
                     ('compile.cgen.backend=llvm', 2), ('sim.color_dirty=true', 2), ('sim.tune.profile=maybe', 2),
                     ('lhd.seed=7', 0), ('sim.init_zero=true', 0)):
        rc, _, err = run(exe_r, ['--set', kv])
        check(rc == want, '`--set %s` -> exit %d (got %d%s)' % (kv, want, rc, (': ' + err.strip()) if rc != want else ''))
    rc, _, err = run(exe_r, ['--set', 'sim.tune.profile=on', '--probe', 'dut.r', '--set', 'sim.tune.profile_dir=' + os.path.join(tmp, 'pp')])
    check('never profiled' in err and not os.path.exists(os.path.join(tmp, 'pp')), 'an observation run is never profiled')

    if FAILS:
        print('%d check(s) FAILED' % len(FAILS))
        return 1
    print('PASS: sim.tune driver contract')
    return 0


if __name__ == '__main__':
    sys.exit(main())
