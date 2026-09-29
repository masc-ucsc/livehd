#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# prpsim: fixture-driven simulation through the same lhd host build as the CLI.
import itertools
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
    # return their directories. Missing required runfiles are a setup failure.
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
    """Run each optional :sim_sweep: k=v1,v2 vector using the declared lhd runtime."""
    name = test.params['name']
    choices = []
    for spec in test.multi.get('sim_sweep', []):
        key, sep, values = spec.strip().partition('=')
        if not sep or not key or not values or any(not v for v in values.split(',')):
            print('{} - simulation - FAILED: invalid :sim_sweep: {}'.format(name, spec))
            return 1
        choices.append([key + '=' + v for v in values.split(',')])
    vectors = list(itertools.product(*choices)) if choices else [()]
    base = runner._scratch(test, 'simulation')
    for i, vector in enumerate(vectors):
        work = os.path.join(base, 'v' + str(i))
        cmd = [runner.lhd, 'sim', test.params['files'][0], '--workdir', work,
               '--set', 'sim.tune.profile=off', '-q'] + runner._extra_sets(test)
        for setting in vector:
            cmd += ['--set', setting]
        for key, value in _parse_args(test):
            cmd += ['--arg', key + '=' + value]
        run = subprocess.run(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if run.returncode:
            print('{} - simulation - FAILED {}: rc={}'.format(name, vector, run.returncode))
            print(run.stdout.decode('utf-8', 'replace'))
            return 1
        report = os.path.join(tmp_dir, work, 'sim', 'sim_tests.json')
        try:
            with open(report) as stream:
                tests = json.load(stream)
            if not tests or any(t.get('status') != 'pass' for t in tests):
                raise ValueError('missing tests or unsuccessful test status')
            names = [t['test'] for t in tests]
            if len(names) != len(set(names)):
                raise ValueError('duplicate test results')
        except (OSError, ValueError, KeyError, TypeError) as exc:
            print('{} - simulation - FAILED: {}'.format(name, exc))
            return 1
        print('{} - simulation - success ({} test(s), {})'.format(name, len(tests), vector or 'default'))
    return 0
