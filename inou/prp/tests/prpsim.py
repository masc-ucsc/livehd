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

import json
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
    # the single `drv.cpp` driver holding every `test` block (`lhd sim
    # --setup-only`), then build it HERMETICALLY with the host C++ compiler and
    # run it. The Slop/Blop runtime is header-only (blop.cpp is empty) and with
    # -DNDEBUG the iassert checks compile out, so the driver has NO link
    # dependencies — no nested bazel, no abseil, no network. The binary runs every
    # test (filtering each test's `--<param>` flags itself), prints `PASS <name>`
    # / `FAIL <name>` per test, and exits non-zero if any test's `assert` fired.
    # (When the header runfiles are absent — a manual harness run outside bazel —
    # fall back to `lhd sim`'s own host-compile of the design, which finds the
    # sibling ../hlop / ../iassert headers, so the manual flow still works.)
    name    = test.params['name']
    prp     = test.params['files'][0]
    simroot = runner._scratch(test, 'simulation')
    simdir  = os.path.join(simroot, 'sim')

    sim_args = _parse_args(test)

    # `:set:` must reach the SIM lowering too. run_simulation builds its command
    # from scratch rather than composing on lhd_upass, so without this a fixture
    # that sets e.g. `upass.roll=true` would assert a rolled graph elsewhere and
    # still SIMULATE the default (unrolled) lowering.
    extra = runner._extra_sets(test)

    setup = [runner.lhd, 'sim', prp, '--setup-only', '--workdir', simroot, '-q'] + extra
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
    drv = os.path.join(abs_simdir, 'drv.cpp')
    if not os.path.exists(drv):
        print('{} - simulation - FAILED: no `drv.cpp` driver generated in {}'.format(name, abs_simdir))
        print(log.decode('utf-8', 'ignore'))
        return 1

    incs = _sim_include_dirs(tmp_dir)
    if not incs:
        # No header runfiles (manual run): let `lhd sim` host-compile the existing
        # setup itself (it finds the sibling ../hlop / ../iassert headers).
        print('{} - simulation - (no header runfiles; lhd sim host-compile fallback)'.format(name))
        cmd  = [runner.lhd, 'sim', prp, '--run-only', '--workdir', simroot] + extra
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
            print('{} - simulation - success (lhd sim host-compile)'.format(name))
        else:
            print('{} - simulation - FAILED (lhd sim host-compile rc={})'.format(name, rc))
            print(log.decode('utf-8', 'ignore'))
        return 0 if rc == 0 else 1

    cxx    = _sim_compiler()
    cflags = ['-std=c++23', '-DNDEBUG', '-O1', '-pthread', '-I' + abs_simdir]
    for d in incs:
        cflags.append('-I' + d)

    # The driver `#include`s the DUT header(s) every test drives; a DUT header in
    # turn `#include`s its sub-module headers (hierarchical designs). Walk the
    # include graph transitively (within simdir) and compile every reachable DUT
    # body -- NOT every emitted unit (a `test` block also lowers to a Slop unit
    # that pulls in formal-only headers it never uses).
    with open(drv) as f:
        drv_src = f.read()
    incs_h  = set()
    pending = list(re.findall(r'#include\s+"([^"]+\.hpp)"', drv_src))
    while pending:
        h = pending.pop()
        if h in incs_h:
            continue
        hp = os.path.join(abs_simdir, h)
        if not os.path.exists(hp):
            continue  # a runtime/header (slop.hpp, vcd_writer.hpp, ...) on the -I path
        incs_h.add(h)
        with open(hp) as hf:
            pending += re.findall(r'#include\s+"([^"]+\.hpp)"', hf.read())
    bodies = [os.path.join(abs_simdir, h[:-4] + '.cpp') for h in sorted(incs_h)
              if os.path.exists(os.path.join(abs_simdir, h[:-4] + '.cpp'))]
    kernel_prefixes = tuple(h[:-4] + '.color-kernel-' for h in incs_h
                            if os.path.exists(os.path.join(abs_simdir, h[:-4] + '.cpp')))
    if kernel_prefixes:
        bodies += [os.path.join(abs_simdir, fn) for fn in sorted(os.listdir(abs_simdir))
                   if fn.endswith('.cpp') and fn.startswith(kernel_prefixes)]
    exe = os.path.join(abs_simdir, 'drv.bin')

    # UNITY BUILD: one `#include`-ing translation unit instead of one clang++ TU
    # per generated body. These fixtures are tiny designs (a few hundred lines
    # each) but every TU re-parses slop.hpp and re-runs the whole frontend, so
    # the per-TU fixed cost -- not the design -- is what this build pays for.
    # Measured 2026-08-15 across all 79 `tests/sim/*.prp` fixtures (arm64,
    # clang -O1): 264s of compile CPU as separate TUs, 148s as unity (1.8x), and
    # every unity binary produced byte-identical output to its multi-TU twin.
    # That CPU is the real lever: `bazel test` runs these targets concurrently,
    # so a `prp-sim-*` target that costs 5s of CPU solo shows up as 20-30s of
    # wall clock in a loaded regression. Compiling the TUs in PARALLEL instead
    # would cut the solo wall time just as well but leave the CPU total
    # untouched (and oversubscribe, since bazel budgets one core per test), so
    # it does nothing for the regression. Unity cuts both.
    #
    # -O1 stays. -O2 is slower to compile with no payoff at these sizes, and -O0
    # does not link: the generated bodies reference `Dlop::free_storage`, which
    # only disappears once the optimizer drops the dead dynamic-width path.
    # Pulling hlop's dlop.cpp/dcontext.cpp in to satisfy it costs more than -O0
    # saves (3.9s vs 2.4s on cgen_cones).
    #
    # A unity TU can only break where separate TUs would not: two generated
    # files colliding at file scope. No fixture does today; if the emitter ever
    # introduces one, fall back to separate TUs rather than turning an emitter
    # hygiene problem into a wall of unrelated red sim failures -- the fallback
    # says so on stdout (captured in the bazel test log) so the lost speed is
    # not silent.
    # NOT inside abs_simdir: `lhd sim --run-only` and the `build.ninja` it writes
    # both take EVERY `*.cpp` in the sim dir except `drv.cpp` as a DUT body, so a
    # unity TU parked there would be compiled as one more body and duplicate every
    # symbol in it -- silently breaking the documented `ninja -C <simdir>` escape
    # hatch on any workdir a `prp-sim-*` target had touched. It only needs to see
    # the generated sources, and it includes them by absolute path.
    # Living in the PARENT means the name is SHARED, so it must carry the identity
    # of the writer -- not of the path, which every writer into that parent has in
    # common. Anything derived from `abs_simdir` is a constant here (`simdir` is
    # always `<simroot>/sim`, and `lhd_kernel_sim.cpp` hardcodes the same), so the
    # only real collision vector -- two harness processes sharing a cwd -- is keyed
    # off the PID. The `finally` below then deletes it, so a passing run leaves no
    # stale TU in a user's workdir. A compile FAILURE keeps it on purpose: the
    # `cmd:` printed below names it, and that line has to stay re-runnable.
    uni = os.path.join(os.path.dirname(abs_simdir.rstrip(os.sep)) or os.curdir,
                       '__prpsim_unity_{}.cpp'.format(os.getpid()))
    keep_uni = False
    try:
        # Inside the `try`: a write that dies part-way (ENOSPC) still gets cleaned.
        with open(uni, 'w') as f:
            f.write('// Generated by prpsim.py -- unity TU over the driver + every reachable DUT body.\n')
            for b in bodies:
                f.write('#include "{}"\n'.format(b))
            f.write('#include "{}"\n'.format(drv))

        cc = [cxx] + cflags + [uni, '-o', exe]
        cp = subprocess.run(cc, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if cp.returncode != 0 and bodies:
            print('{} - simulation - NOTE: unity build failed (generated files collide at file '
                  'scope?); falling back to one TU per body, which is slower'.format(name))
            print(cp.stdout.decode('utf-8', 'ignore'))
            cc = [cxx] + cflags + bodies + [drv, '-o', exe]
            cp = subprocess.run(cc, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if cp.returncode != 0:
            print('{} - simulation - FAILED: driver did not compile'.format(name))
            print('  cmd: {}'.format(' '.join(cc)))
            print(cp.stdout.decode('utf-8', 'ignore'))
            # `cc` still names `uni` when there are no bodies to fall back to.
            keep_uni = uni in cc
            return 1

        # Run the one binary over every test. The `:args:` are passed verbatim as
        # `--<key> <value>`; the binary applies each per test (warning about a flag no
        # test uses) and exits non-zero if any test's `assert` fired.
        run_args = []
        for k, v in sim_args:
            run_args += ['--' + k, v]
        rp  = subprocess.run([exe] + run_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = rp.stdout.decode('utf-8', 'ignore')
        if rp.returncode != 0:
            print('{} - simulation - FAILED (assert):'.format(name))
            print(out)
            return 1
        # rc == 0 means every test passed. Report the count from the binary's own
        # registry (`--list-tests` JSON) rather than scanning stdout for "PASS " lines
        # (a test's own `puts("PASS ...")` would otherwise miscount).
        n_tests = 0
        try:
            lt = subprocess.run([exe, '--list-tests'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            n_tests = len(json.loads(lt.stdout.decode('utf-8', 'ignore')).get('tests', []))
        except Exception:
            n_tests = out.count('\nPASS ') + (1 if out.startswith('PASS ') else 0)
        print('{} - simulation - success ({} test(s))'.format(name, n_tests))
        return 0
    finally:
        # The unity compiler errors are printed inside the `try` above, so they are
        # already out. The one diagnostic that OUTLIVES this scope is the `cmd:`
        # line on a compile failure with no fallback -- `keep_uni` leaves the TU in
        # place for it.
        if not keep_uni:
            try:
                os.remove(uni)
            except OSError:
                pass
