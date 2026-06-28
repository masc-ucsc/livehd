#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpsim — the `:type: simulation` test owner.
#
# Split out of prplib.py so all `lhd sim` regression logic lives in one place:
# lower a design's DUT(s) to Slop<N> C++ (`lhd sim --setup-only`), build each
# generated `test`-block driver HERMETICALLY with the host C++ compiler, and run
# it (a non-zero exit == an assert fired). `prplib.PrpRunner.run` dispatches the
# `simulation` mode here (lazy import, so only the `prp-sim-*` targets need this
# module in their runfiles).
#
# `:args: k=v k=v` (a TEST-HARNESS-ONLY header tag — `lhd sim` itself never reads
# the .prp header) binds each `test name(params)` parameter: every parameter is a
# runtime `--<name>` flag on the generated driver, supplied here when the driver
# runs (NOT baked in at setup), so a parameter with no default that is never
# given makes the driver print its usage and fail.

import glob
import os
import re
import shutil
import subprocess


def _sim_compiler():
    # Host C++ compiler for the hermetic driver build. The Slop runtime needs
    # C++23 (<print>, std::format); the repo already requires a C++23 toolchain
    # to build lhd, so the host compiler has it. $CXX wins for CI overrides.
    for c in (os.environ.get('CXX'), 'clang++', 'c++', 'g++'):
        if c and shutil.which(c):
            return c
    return 'c++'


def _sim_include_dirs(tmp_dir):
    # Locate the hlop + iassert header dirs. Under `bazel test` the
    # `cc_direct_headers` data dep stages slop.hpp/blop.hpp (hlop) and
    # iassert.hpp (iassert) into the test runfiles; find them by name and
    # return their directories. Returns [] when not found (manual run with no
    # runfiles) so the caller can fall back to the nested-bazel build.
    roots = []
    for env in ('TEST_SRCDIR', 'RUNFILES_DIR'):
        v = os.environ.get(env)
        if v and os.path.isdir(v):
            roots.append(v)
    roots.append(tmp_dir)
    wanted = ('slop.hpp', 'iassert.hpp')
    found = {}
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            for w in wanted:
                if w not in found and w in files:
                    found[w] = dirpath
        if len(found) == len(wanted):
            break
    if len(found) != len(wanted):
        return []
    # dedup while preserving order
    dirs, seen = [], set()
    for w in wanted:
        d = found[w]
        if d not in seen:
            seen.add(d)
            dirs.append(d)
    return dirs


def _parse_args(test):
    # `:args: k=v k=v` -> list of (key, value). A token without `=` is ignored.
    out = []
    for tok in test.params.get('args', '').split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            out.append((k, v))
    return out


def run_simulation(runner, tmp_dir, test):
    # `:type: simulation`: lower the design's DUT(s) to Slop<N> C++ and generate
    # a driver per `test` block (`lhd sim --setup-only`), then build each driver
    # HERMETICALLY with the host C++ compiler and run it. The Slop/Blop runtime
    # is header-only (blop.cpp is empty) and with -DNDEBUG the iassert checks
    # compile out, so a driver has NO link dependencies — no nested bazel, no
    # abseil, no network. A non-zero driver exit means an `assert` fired in that
    # test. (When the header runfiles are absent — a manual harness run outside
    # bazel — fall back to `lhd sim`'s own nested bazel build so the manual flow
    # still works.)
    name    = test.params['name']
    prp     = test.params['files'][0]
    simroot = runner._scratch(test, 'simulation')
    simdir  = os.path.join(simroot, 'sim')

    sim_args = _parse_args(test)

    setup = [runner.lhd, 'sim', prp, '--setup-only', '--workdir', simroot, '-q']
    proc  = subprocess.Popen(setup, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        log, _ = proc.communicate()
        rc = proc.returncode
    except Exception:
        proc.kill()
        rc, log = 1, b''
    if rc != 0:
        print('{} - simulation - FAILED: `lhd sim --setup-only` rc={}'.format(name, rc))
        print(log.decode('utf-8', 'ignore'))
        return 1

    abs_simdir = simdir if os.path.isabs(simdir) else os.path.join(tmp_dir, simdir)
    drivers = sorted(glob.glob(os.path.join(abs_simdir, 'drv_*.cpp')))
    if not drivers:
        print('{} - simulation - FAILED: no `test` drivers generated in {}'.format(name, abs_simdir))
        print(log.decode('utf-8', 'ignore'))
        return 1

    incs = _sim_include_dirs(tmp_dir)
    if not incs:
        # No header runfiles (manual run): use lhd sim's nested-bazel build of
        # the already-generated setup so the manual flow still works.
        print('{} - simulation - (no header runfiles; nested-bazel fallback)'.format(name))
        cmd  = [runner.lhd, 'sim', prp, '--run-only', '--workdir', simroot]
        for k, v in sim_args:
            cmd += ['--arg', '{}={}'.format(k, v)]
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log, _ = proc.communicate()
            rc = proc.returncode
        except Exception:
            proc.kill()
            rc, log = 1, b''
        if rc == 0:
            print('{} - simulation - success ({} test(s), nested-bazel)'.format(name, len(drivers)))
        else:
            print('{} - simulation - FAILED (nested-bazel rc={})'.format(name, rc))
            print(log.decode('utf-8', 'ignore'))
        return 0 if rc == 0 else 1

    cxx    = _sim_compiler()
    cflags = ['-std=c++23', '-DNDEBUG', '-O1', '-I' + abs_simdir]
    for d in incs:
        cflags.append('-I' + d)

    failed = 0
    for drv in drivers:
        base = os.path.splitext(os.path.basename(drv))[0]
        tn   = base[len('drv_'):] if base.startswith('drv_') else base
        exe  = os.path.join(abs_simdir, base + '.bin')
        # The driver `#include`s exactly the DUT header(s) it drives -> compile
        # only those DUT bodies (NOT every emitted unit: a `test` block also
        # lowers to a Slop unit that pulls in formal-only headers it never uses).
        with open(drv) as f:
            drv_src = f.read()
        incs_h = set(re.findall(r'#include\s+"([^"]+\.hpp)"', drv_src))
        bodies = [os.path.join(abs_simdir, h[:-4] + '.cpp') for h in sorted(incs_h)
                  if os.path.exists(os.path.join(abs_simdir, h[:-4] + '.cpp'))]
        cc   = [cxx] + cflags + bodies + [drv, '-o', exe]
        cp   = subprocess.run(cc, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if cp.returncode != 0:
            print('{} - simulation - {} FAILED: driver did not compile'.format(name, tn))
            print('  cmd: {}'.format(' '.join(cc)))
            print(cp.stdout.decode('utf-8', 'ignore'))
            failed += 1
            continue
        # Bind the `:args:` this driver actually accepts (each test may have a
        # different parameter set; pass only the flags this driver declares).
        accepts = set(re.findall(r'_key == "--([A-Za-z_]\w*)"', drv_src))
        run_args = []
        for k, v in sim_args:
            if k in accepts:
                run_args += ['--' + k, v]
        rp  = subprocess.run([exe] + run_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = rp.stdout.decode('utf-8', 'ignore')
        if rp.returncode != 0:
            print('{} - simulation - {} FAILED (assert):'.format(name, tn))
            print(out)
            failed += 1
        else:
            print('{} - simulation - {} success'.format(name, tn))

    if failed:
        print('{} - simulation - FAILED: {} of {} test(s) failed'.format(name, failed, len(drivers)))
        return 1
    print('{} - simulation - success ({} test(s))'.format(name, len(drivers)))
    return 0
