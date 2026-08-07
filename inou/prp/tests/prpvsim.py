#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpvsim — the VERILATOR DIFFERENTIAL for `inou/prp/tests/sim/` fixtures.
#
# WHY THIS EXISTS. A `tests/sim/` fixture checks `lhd sim` against values a human
# derived. When `lhd sim` REFUSES a design (`gated-clock-unsupported`,
# `combinational-loop`) or returns wrong numbers, the fixture alone cannot say
# whether the design is unreasonable or the simulator is wrong. This runs the
# SAME design through an independent event-driven simulator, so a fixture can
# demonstrate exactly that: **fails under `lhd sim`, passes under Verilator**.
#
# WHY IT CANNOT REUSE `lhd sim`'s DRIVER GENERATOR. The interesting fixtures are
# refused BY `inou.cgen.sim`, and `lhd sim --setup-only` runs that pass before it
# writes `drv.cpp` — so no driver is ever generated for the cases that matter. To
# stay reachable, this path never touches cgen.sim at all:
#
#     lhd compile <fixture>.prp --emit verilog:dut.v      (inou.cgen.verilog)
#     verilator --cc --exe --build dut.v <twin>.cpp
#     ./V<top>                                            (exit 0 == pass)
#
# The C++ twin is a hand-written mirror of the fixture's `test` block — the same
# discipline lhdsuite uses for `dino_prog_tb.prp` / `dino_prog_tb_verilator.cpp`.
# It has to be hand written for one reason worth stating: a `tick` body's `step`
# is ONE EVENT in `lhd sim` and TWO in an event-driven simulator (drive inputs
# while the clock is low, `eval()` at the rise, `eval()` again at the fall), and
# a design with negedge state or an active-low clock gate is only correct if the
# driver performs both. See `advance_clock` in any of the twins.
#
# A fixture opts in with two header tags:
#     :verilator: <twin>.cpp     file under tests/sim/verilator/
#     :vtop: <module>            emitted top module (default: the `pub mod` name)

import os
import re
import shutil
import subprocess


# Where verilator actually lives when it is not on the test PATH. bazel runs a
# test with a sanitized PATH that carries none of the usual install prefixes, so
# without this a machine that HAS verilator reports SKIP -- and a SKIP that
# reads as PASS is exactly how a differential silently stops differentiating.
_VERILATOR_PREFIXES = (
    '/opt/homebrew/bin/verilator',  # macOS, homebrew on apple silicon
    '/usr/local/bin/verilator',     # macOS intel homebrew, manual installs
    '/usr/bin/verilator',           # apt / dnf
    '/opt/local/bin/verilator',     # macports
)


def verilator_bin():
    # $VERILATOR wins (bazel passes it through the test sandbox), then PATH,
    # then the install prefixes bazel's PATH drops.
    v = os.environ.get('VERILATOR')
    if v and (os.path.isfile(v) or shutil.which(v)):
        return v
    found = shutil.which('verilator')
    if found:
        return found
    for cand in _VERILATOR_PREFIXES:
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    return None


def _pub_mod_name(prp_path):
    # The emitted TOP is the fixture's `pub mod`. Deriving it from the source
    # beats reading the .v: LiveHD emits children FIRST, so "the first module in
    # the file" is a leaf, not the top.
    try:
        with open(prp_path) as f:
            m = re.search(r'^\s*pub\s+mod\s+([A-Za-z_][A-Za-z_0-9]*)', f.read(), re.M)
            return m.group(1) if m else None
    except OSError:
        return None


def _twin_path(runner, test, twin):
    # The twin sits next to the fixture, under tests/sim/verilator/. Probe the
    # fixture's own directory first (manual runs), then the bazel runfiles.
    prp = test.params['files'][0]
    cands = [os.path.join(os.path.dirname(prp), 'verilator', twin)]
    for env in ('TEST_SRCDIR', 'RUNFILES_DIR'):
        root = os.environ.get(env)
        if root and os.path.isdir(root):
            for dirpath, _dirs, files in os.walk(root):
                if twin in files and os.path.basename(dirpath) == 'verilator':
                    cands.append(os.path.join(dirpath, twin))
    for c in cands:
        if os.path.exists(c):
            return os.path.abspath(c)
    return None


def run_verilator_diff(runner, tmp_dir, test):
    """`:verilator:` — emit the fixture's Verilog and run its C++ twin under
    Verilator. Returns 0 on pass, 1 on failure, 0 (with a SKIP line) when
    verilator is not installed."""
    name = test.params['name']
    prp  = test.params['files'][0]
    twin = test.params.get('verilator', '').strip()

    vbin = verilator_bin()
    if not vbin:
        # Verilator is an OUTSIDE point of comparison, not something this repo
        # ships. A machine without it SKIPS rather than failing the suite -- but
        # the line has to say plainly that NOTHING WAS COMPARED, because bazel
        # reports this target as PASSED either way.
        print('{} - vsim - SKIP: verilator not installed, so the differential DID NOT RUN '
              '(brew/apt install verilator, or export VERILATOR=<path>)'.format(name))
        return 0

    # ABSOLUTE throughout: `lhd compile` runs with cwd=tmp_dir while verilator
    # runs with cwd=work, so a relative scratch path would resolve against two
    # different roots (and verilator fails with an opaque "Can't write file").
    work = runner._scratch(test, 'vsim')
    work = work if os.path.isabs(work) else os.path.abspath(os.path.join(tmp_dir, work))
    os.makedirs(work, exist_ok=True)
    vfile = os.path.join(work, 'dut.v')

    # 1. Pyrope -> Verilog. This is `inou.cgen.verilog`, NOT `inou.cgen.sim`, so
    #    a fixture cgen.sim refuses still gets here.
    cmd = [runner.lhd, 'compile', prp, '--emit', 'verilog:' + vfile,
           '--workdir', os.path.join(work, 'w')]
    cp = subprocess.run(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if cp.returncode != 0 or not os.path.exists(vfile):
        print('{} - vsim - FAILED: `lhd compile --emit verilog` rc={}'.format(name, cp.returncode))
        print('  cmd: {}'.format(' '.join(cmd)))
        print(cp.stdout.decode('utf-8', 'ignore'))
        return 1

    top = test.params.get('vtop', '').strip() or _pub_mod_name(
        prp if os.path.isabs(prp) else os.path.join(tmp_dir, prp))
    if not top:
        print('{} - vsim - FAILED: no `:vtop:` and no `pub mod` in {}'.format(name, prp))
        return 1

    tpath = _twin_path(runner, test, twin)
    if not tpath:
        print('{} - vsim - FAILED: twin driver `{}` not found under tests/sim/verilator/'.format(name, twin))
        return 1

    # 2. verilate + build. --build runs the generated make itself, which keeps
    #    this one step; -Wno-fatal because LiveHD's emitter is not lint-clean and
    #    lint is not what is under test here (a REAL circular-logic finding still
    #    shows as a UNOPTFLAT warning in the log, which is exactly the signal a
    #    comb-loop differential wants to be able to read).
    obj = os.path.join(work, 'obj')
    vcmd = [vbin, '--cc', '--exe', '--build', '-Wno-fatal', '-O2',
            '--Mdir', obj, '--top-module', top,
            '-CFLAGS', '-std=c++17 -O1',
            os.path.abspath(vfile), tpath]
    cp = subprocess.run(vcmd, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    vlog = cp.stdout.decode('utf-8', 'ignore')
    if cp.returncode != 0:
        print('{} - vsim - FAILED: verilator rc={}'.format(name, cp.returncode))
        print('  cmd: {}'.format(' '.join(vcmd)))
        print(vlog)
        return 1

    exe = os.path.join(obj, 'V' + top)
    if not os.path.exists(exe):
        print('{} - vsim - FAILED: verilator produced no {}'.format(name, exe))
        print(vlog)
        return 1

    # 3. run. The twin returns non-zero on its first mismatch and prints it.
    rp  = subprocess.run([exe], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = rp.stdout.decode('utf-8', 'ignore')
    if rp.returncode != 0:
        print('{} - vsim - FAILED under verilator (rc={}):'.format(name, rp.returncode))
        print(out)
        return 1

    unopt = 'UNOPTFLAT' in vlog
    print('{} - vsim - success (verilator {}{})'.format(
        name, os.path.basename(vbin),
        ', UNOPTFLAT present — verilator sees a REAL settle loop' if unopt else ''))
    if out.strip():
        print(out.rstrip())
    return 0
