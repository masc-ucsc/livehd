#!/usr/bin/env python3

import argparse
import copy
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys

from lec import run_lec, verdict as lec_verdict

class PrpTest:
    """
    Pyrope Test Object
    """
    def __init__(self, prp_file):
        # Set default values
        self.params = {}
        # Every occurrence of every tag, in file order. `params` keeps the LAST
        # value (as it always has); tags that are naturally a LIST — an
        # expectation stated once per line, e.g. `:lec_grep:` — read `multi`
        # instead, so they need no in-value separator to hide spaces from.
        self.multi  = {}
        self.params['name']       = os.path.basename(prp_file)
        self.params['files']      = prp_file
        self.params['incdirs']    = os.path.dirname(prp_file)
        self.params['top_module'] = 'top'
        self.params['defines']    = ''
        self.params['type']       = 'parsing'

        # Extract parameters in pyrope file
        try:
            with open(prp_file) as f:
                for line in f:
                    param = re.search(r'^:([a-zA-Z_-]+):\s*(.+)', line)
                    if param == None:
                        continue

                    param_name = param[1]
                    param_value = param[2]

                    self.params[param_name] = param_value
                    self.multi.setdefault(param_name, []).append(param_value)
        except Exception as e:
            print('Failed to process "{}"'.format(prp_file))
            sys.exit(1)

        # Post-process
        self.params['files'] = self.params['files'].split()
        self.params['incdirs'] = self.params['incdirs'].split()
        self.params['type'] = self.params['type'].split()

class PrpRunner:
    """
    LiveHD Pyrope Compilation Runner
    """

    def __init__(self, bitfuzz=False):
        self.equiv_sets = ["compile.bitfuzz.mode=wires"] if bitfuzz else []
        # Tests drive the lhd kernel (one stateless invocation per mode); the
        # lgshell REPL is no longer involved.
        if os.path.exists("./bazel-bin/lhd/lhd"):
            self.lhd = "./bazel-bin/lhd/lhd"
        elif os.path.exists("./lhd/lhd"):
            self.lhd = "./lhd/lhd"
        else:
            print('Failed to find the lhd binary')
            sys.exit(3)

    @staticmethod
    def _safe_name(test):
        return re.sub(r'\W+', '_', test.params['name'])

    def _scratch_path(self, test, mode, suffix=''):
        # `tmp*` prefix so the dirs are .gitignore-covered when the harness is
        # run manually from the repo root (they are created under cwd).
        return 'tmp_lhd_{}_{}{}'.format(self._safe_name(test), mode, suffix)

    def _scratch(self, test, mode, suffix=''):
        path = self._scratch_path(test, mode, suffix)
        shutil.rmtree(path, ignore_errors=True)
        return path

    def lhd_parse(self, test, mode):
        # Front-end only: inou.prp + pass.lnastfmt, no upass ('parsing' and
        # 'lnast' are the same surface; the old parse_only flag was removed).
        # `lhd elaborate` is gone (folded into `lhd compile`), and compile
        # always runs pass.upass — so gut upass to a no-op (order=noop) to keep
        # this tier a pure front-end parse check, no emit (so no tolg either).
        cmd = [self.lhd, 'compile']
        cmd += test.params['files']
        cmd += ['--workdir', self._scratch(test, mode), '-q']
        cmd += ['--set', 'upass.order=noop', '--set', 'upass.verifier=false']
        return cmd

    def lhd_upass(self, test, mode):
        # Pipeline smoke-test: runs constprop only, with the verifier turned
        # OFF explicitly. The CLI default is verifier:on (it hard-errors on a
        # comptime-false cassert), but this mode exists because constprop has
        # known gaps (tuple index, enum values, string ops, __wrap/__ubits
        # attrs, ...) that fold some casserts incorrectly — so these tests just
        # assert the pipeline doesn't crash, never that the casserts hold. For
        # correctness checking, use `:type: comptime`.
        # No emits -> inou.prp + lnastfmt + pass.upass, tolg skipped. A bare
        # `lhd compile FILE` now lowers to LGraphs for max diagnostics even
        # without an emit (force_diag_graphs), so this stage-scoped tier opts
        # out explicitly with `upass.tolg=false`: a comptime/upass test program
        # is checked for evaluation, not synthesized (many are pure comptime
        # programs that call combs with constant args — not hardware modules).
        cmd = [self.lhd, 'compile']
        cmd += test.params['files']
        cmd += ['--workdir', self._scratch(test, mode), '-q']
        cmd += ['--set', 'upass.verifier=false', '--set', 'upass.tolg=false']
        cmd += self._extra_sets(test)
        cmd += self.equiv_set_args()
        return cmd

    @staticmethod
    def _extra_sets(test):
        """`:set: a.b=c d.e=f` — per-fixture pass flags, appended to every mode.

        Used by fixtures that must exercise a non-default lowering (e.g.
        `:set: compile.unroll=true` to unroll a source loop instead of keeping it rolled, or
        unrolling it).
        """
        spec = test.params.get('set')
        if not spec:
            return []
        out = []
        for tok in spec.split():
            out += ['--set', tok]
        return out

    @staticmethod
    def _without_setting(cmd, key):
        """Remove a builder override so this stage uses the CLI default."""
        out, i = [], 0
        while i < len(cmd):
            if cmd[i] == '--set' and i + 1 < len(cmd) and cmd[i + 1].split('=')[0] == key:
                i += 2
                continue
            out.append(cmd[i])
            i += 1
        return out

    def lhd_comptime(self, test, mode):
        # Pure compile-time program: every cassert must resolve. The verifier
        # uses its enabled CLI default after removing the smoke tier disable;
        # it hard-errors on known-false cassert and discharges
        # known-true. To opt out for a specific case, drop `:type: comptime`
        # back to `:type: upass`.
        #
        # Optional header tags (read via PrpTest.params):
        #   :verifier_pass: N   — expected count of discharged casserts
        #   :verifier_fail: N   — expected count of known-false casserts
        # When set, the verifier end_run compares its tally and fails the
        # test if they don't match. -1 or absent disables the check.
        cmd = self._without_setting(self.lhd_upass(test, mode), 'upass.verifier')
        for tag in ('verifier_pass', 'verifier_fail', 'verifier_include_funcs'):
            if tag in test.params:
                cmd += ['--set', 'upass.{}={}'.format(tag, test.params[tag])]
        return cmd

    def lhd_error(self, test, mode):
        # Expected-failure test: the program must trigger a compile error. The
        # header's :error: / :help: regexes are matched against the emitted
        # diagnostic's message / hint (see run_error()). Runs the full
        # prp->upass pipeline (verifier on, as the old bare pass.upass default)
        # so an error at any stage (parse, upass) is caught. run_error() adds
        # the --emit diagnostics: slot itself.
        #
        # Optional `:tolg: 1` header tag (task 1r): extend the pipeline with
        # the LNAST->LGraph compilation (lg: emit) so errors that
        # only fire at tolg (e.g. a func_call with no hardware lowering) are
        # exercisable as error tests.
        cmd = self._without_setting(self.lhd_upass(test, mode), 'upass.verifier')
        if test.params.get('tolg'):
            cmd += ['--emit-dir',
                    'lg:{}/'.format(self._scratch(test, mode, '_lg'))]
        # Optional `:error_top: <name>` — pass an explicit `--top`. Some
        # diagnostics only fire for a top the USER named (a generic entry point
        # with no declaration default is a real error when it was requested as
        # the top, and a library template waiting for its caller when it was
        # merely the only unit in the file), so the fixture has to be able to
        # say which of the two it is pinning. A distinct tag, not `:top_module:`,
        # because that one defaults to 'top' for every test.
        if test.params.get('error_top'):
            cmd += ['--top', test.params['error_top'].strip()]
        return cmd

    def lhd_warning(self, test, mode):
        # Expected-warning test: the program must COMPILE CLEANLY (no error) and
        # the front-end must emit a warning diagnostic. The pipeline mirrors the
        # upass smoke-test (constprop only, verifier off, no tolg) — the warning
        # is produced during prp->lnast lowering, so the front-end stage alone is
        # enough; run_warning() adds the --emit diagnostics: slot itself.
        return self.lhd_upass(test, mode)

    def lhd_lgraph(self, test, mode):
        # LNAST->LGraph: the lg: emit gates the kernel's standalone tolg
        # lowering followed by the standard graph optimization passes
        # (tolg + pass.cprop + pass.bitwidth). With the optimization-level
        # selection gone there is only ONE lowering command, so the two mode
        # names below resolve to it rather than drifting apart.
        cmd = self.lhd_upass(test, mode)
        cmd += ['--emit-dir',
                'lg:{}/'.format(self._scratch(test, mode, '_lg'))]
        return cmd

    def lhd_lg_compile(self, test, mode):
        return self.lhd_lgraph(test, mode)

    def lhd_equiv(self, test, odir):
        # Equivalence test: compile to optimized LGraph and emit
        # per-module Verilog into `odir`. run_equiv() then LECs the generated
        # Verilog against the sibling golden `.v` via default lhd lec.
        #
        # Optional `:reset_style: async` header tag (task 2d-reg): set the
        # upass.reset_style elaboration flag so the implicit-reset flops wire
        # an async reset and the golden can assert the async always-block.
        cmd = self.lhd_upass(test, 'equiv')
        # The emitted side is never satopt-optimized (the compile default,
        # pinned): the LEC below runs every satopt stage, so each pair also
        # checks satopt's rewrites against an unoptimized design.
        cmd += ['--set', 'pass.satopt=false']
        if test.params.get('reset_style', 'sync') != 'sync':
            cmd += ['--set', 'upass.reset_style={}'.format(test.params['reset_style'])]
        if test.params.get('compile_top'):
            cmd += ['--top', test.params['compile_top'].strip()]
        cmd += ['--emit-dir', 'verilog:{}/'.format(odir)]
        return cmd

    def equiv_set_args(self):
        return [arg for setting in self.equiv_sets for arg in ('--set', setting)]

    @staticmethod
    def lec_satopt_args():
        # LEC re-reads both Verilog sides with every satopt stage on (shared
        # profile): a wrong rewrite on either side refutes the pair. A fixture's
        # `:set:` comes after and may override it.
        return ['--set', 'pass.satopt=true', '--set', 'pass.satopt.stages=all']

    def gen_lhd_cmd(self, test, mode):
        gen_cmd = {
            'parsing'  : self.lhd_parse,
            'lnast'    : self.lhd_parse,
            'upass'    : self.lhd_upass,
            'comptime' : self.lhd_comptime,
            'error'    : self.lhd_error,
            'lgraph'   : self.lhd_lgraph,
            'compile'  : self.lhd_lg_compile,
            'simulation': self.lhd_simulation,
        }

        return gen_cmd[mode](test, mode)

    def lhd_simulation(self, test, mode):
        # `lhd sim FILE` lowers each `test` block's DUT to a Slop C++ sim,
        # generates a driver per test (tick loop + runtime asserts), then
        # bazel-builds and runs them. Exit 0 = every assert held. No --workdir:
        # the generated bazel module then lands in an OS-temp dir (outside the
        # repo), so a later `bazel build //...` here never sweeps it.
        cmd = [self.lhd, 'sim']
        cmd += test.params['files']
        return cmd

    @staticmethod
    def _pattern_matches(pattern, text):
        # The header :error:/:help: value is a regex (re.search). If it is not a
        # valid regex (e.g. `')'` has an unbalanced paren), fall back to a literal
        # substring match so authors can write the offending token verbatim.
        try:
            return re.search(pattern, text) is not None
        except re.error:
            return re.search(re.escape(pattern), text) is not None

    def run_error(self, tmp_dir, test: PrpTest):
        # Expected-failure test: the program MUST emit a compile error whose
        # message/hint match the header :error:/:help: regexes. Diagnostics
        # are read from a JSONL file — structured + crash-safe, so it survives
        # the dbg abort that a fatal error triggers. Under lhd the sink path
        # is the declared `--emit diagnostics:` slot (the kernel ignores the
        # old LIVEHD_DIAG env: no ambient state).
        cmd       = self.gen_lhd_cmd(test, 'error')
        safe_name = self._safe_name(test)
        diag_path = os.path.join(tmp_dir, 'diag_{}.jsonl'.format(safe_name))
        if os.path.exists(diag_path):
            os.remove(diag_path)
        cmd += ['--emit', 'diagnostics:' + diag_path]

        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log, _ = proc.communicate()
        except Exception:
            proc.kill()
            log = b''

        errors = []
        if os.path.exists(diag_path):
            with open(diag_path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except ValueError:
                        continue
                    if rec.get('severity') == 'error':
                        errors.append(rec)

        name = test.params['name']
        if not errors:
            print('{} - error - FAILED: expected a compile error, none was emitted'.format(name))
            print(log.decode('utf-8', 'ignore'))
            return 1

        messages = ' || '.join(e.get('message', '') for e in errors)
        hints    = ' || '.join(e.get('hint', '') for e in errors)

        epat = test.params.get('error')
        if epat is not None and not self._pattern_matches(epat, messages):
            print('{} - error - FAILED: :error: /{}/ did not match emitted error(s):'.format(name, epat))
            print('  emitted: {}'.format(messages))
            return 1

        hpat = test.params.get('help')
        if hpat is not None and not self._pattern_matches(hpat, hints):
            print('{} - error - FAILED: :help: /{}/ did not match emitted hint(s):'.format(name, hpat))
            print('  emitted: {}'.format(hints))
            return 1

        # Optional line check: a comment containing `locate_error_here` marks the
        # line where the error is expected. Using a marker (instead of a hard-coded
        # line number) keeps the test correct when lines are added/removed above.
        marker_lines = self._find_marker_lines(test)
        if marker_lines:
            error_lines = set()
            for e in errors:
                span = e.get('span') or {}
                if isinstance(span, dict) and span.get('start_line') is not None:
                    error_lines.add(span['start_line'])
            missing = [ln for ln in marker_lines if ln not in error_lines]
            if missing:
                print('{} - error - FAILED: locate_error_here at line(s) {} but error(s) reported at {}'.format(
                    name, missing, sorted(error_lines) if error_lines else '(no located error)'))
                return 1

        print('{} - error - success (matched: {})'.format(name, messages))
        return 0

    def run_warning(self, tmp_dir, test: PrpTest):
        # Expected-warning test (the warnings/ counterpart of run_error): the
        # program MUST compile cleanly (no error, exit 0) AND emit at least one
        # WARNING diagnostic whose message/hint match the header
        # :warning:/:help: regexes. A `locate_warning_here` marker pins the
        # warning's line. Diagnostics come from the same JSONL sink as errors.
        cmd       = self.lhd_warning(test, 'warning')
        safe_name = self._safe_name(test)
        diag_path = os.path.join(tmp_dir, 'diag_{}.jsonl'.format(safe_name))
        if os.path.exists(diag_path):
            os.remove(diag_path)
        cmd += ['--emit', 'diagnostics:' + diag_path]

        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log, _ = proc.communicate()
            rc = proc.returncode
        except Exception:
            proc.kill()
            log, rc = b'', 1

        warnings, errors = [], []
        if os.path.exists(diag_path):
            with open(diag_path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except ValueError:
                        continue
                    sev = rec.get('severity')
                    if sev == 'warning':
                        warnings.append(rec)
                    elif sev == 'error':
                        errors.append(rec)

        name = test.params['name']
        # A warning test pins a clean compile: an error means the program is
        # broken, not merely lint-worthy (that case belongs in tests/errors/).
        if errors or rc != 0:
            emsg = ' || '.join(e.get('message', '') for e in errors) or '(no error diagnostic; rc={})'.format(rc)
            print('{} - warning - FAILED: expected a clean compile, got error(s): {}'.format(name, emsg))
            print(log.decode('utf-8', 'ignore'))
            return 1
        if not warnings:
            print('{} - warning - FAILED: expected a compile warning, none was emitted'.format(name))
            print(log.decode('utf-8', 'ignore'))
            return 1

        messages = ' || '.join(w.get('message', '') for w in warnings)
        hints    = ' || '.join(w.get('hint', '') for w in warnings)

        wpat = test.params.get('warning')
        if wpat is not None and not self._pattern_matches(wpat, messages):
            print('{} - warning - FAILED: :warning: /{}/ did not match emitted warning(s):'.format(name, wpat))
            print('  emitted: {}'.format(messages))
            return 1

        hpat = test.params.get('help')
        if hpat is not None and not self._pattern_matches(hpat, hints):
            print('{} - warning - FAILED: :help: /{}/ did not match emitted hint(s):'.format(name, hpat))
            print('  emitted: {}'.format(hints))
            return 1

        # Optional line check: a comment containing `locate_warning_here` marks
        # the line where the warning is expected (marker, not a hard-coded line
        # number — survives edits above it).
        marker_lines = self._find_marker_lines(test, 'locate_warning_here')
        if marker_lines:
            warn_lines = set()
            for w in warnings:
                span = w.get('span') or {}
                if isinstance(span, dict) and span.get('start_line') is not None:
                    warn_lines.add(span['start_line'])
            missing = [ln for ln in marker_lines if ln not in warn_lines]
            if missing:
                print('{} - warning - FAILED: locate_warning_here at line(s) {} but warning(s) reported at {}'.format(
                    name, missing, sorted(warn_lines) if warn_lines else '(no located warning)'))
                return 1

        print('{} - warning - success (matched: {})'.format(name, messages))
        return 0

    @staticmethod
    def _verilog_modules(vpath):
        # Names (unescaped, without the leading `\`) of all modules declared in
        # a verilog file, in declaration order. Stops the name at whitespace or
        # `(` so `module \foo.bar(` and `module foo (` both yield `foo.bar`/`foo`.
        try:
            with open(vpath) as f:
                text = f.read()
        except OSError:
            return []
        # Anchored at the START OF A LINE, deliberately: an unanchored `\bmodule\s+`
        # matches the word inside a golden's own PROSE COMMENT ("the module carries"
        # -> top 'carries'), which is a header comment away from silently picking the
        # wrong top. Every Verilog module declaration begins its line.
        return re.findall(r'^\s*module\s+\\?([^\s(]+)', text, re.M)

    @staticmethod
    def _verilog_top_module(vpath):
        # Name (unescaped) of the first module declared in a verilog file.
        mods = PrpRunner._verilog_modules(vpath)
        return mods[0] if mods else None

    def run_equiv(self, tmp_dir, test: PrpTest):
        # Lower each .prp function to Verilog (inou.prp -> pass.upass tolg:1 ->
        # inou.cgen.verilog) and prove it equivalent to its sibling golden .v
        # via default lhd lec (formal LEC). The generated module names are
        # the function-tree names (e.g. trivial_if.fun3); the golden must
        # declare the same module name so LEC selects the same top.
        name = test.params['name']
        prp  = test.params['files'][0]
        gold = os.path.splitext(prp)[0] + '.v'
        gold_abs = gold if os.path.isabs(gold) else os.path.join(tmp_dir, gold)

        if not os.path.exists(gold_abs):
            print('{} - equiv - FAILED: no golden verilog {}'.format(name, gold))
            return 1

        safe_name = re.sub(r'\W+', '_', name)
        # `tmp*` prefix so the generated dir is covered by the repo .gitignore
        # when the harness is run manually from the repo root.
        odir = os.path.join(tmp_dir, 'tmp_equiv_' + safe_name)
        shutil.rmtree(odir, ignore_errors=True)
        os.makedirs(odir, exist_ok=True)

        cmd  = self.lhd_equiv(test, odir)
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log, _ = proc.communicate()
            rc = proc.returncode
        except Exception:
            proc.kill()
            rc, log = 1, b''
        if rc != 0:
            print('{} - equiv - FAILED: prp->verilog pipeline rc={}'.format(name, rc))
            print(log.decode('utf-8', 'ignore'))
            return 1

        gen_vs = sorted(v for v in glob.glob(os.path.join(odir, '*.v')))
        if not gen_vs:
            print('{} - equiv - FAILED: no verilog generated in {}'.format(name, odir))
            print(log.decode('utf-8', 'ignore'))
            return 1

        impl = os.path.join(odir, 'all_' + safe_name + '_impl.v')
        with open(impl, 'w') as out:
            for v in gen_vs:
                with open(v) as f:
                    out.write(f.read())
                    out.write('\n')

        # The reference (golden .v) and implementation (pyrope-generated .v) may
        # use DIFFERENT module names, and a .prp may generate several modules.
        # The header pins which module to compare on each side:
        #   :verilog_top:  module name in the golden .v   (reference side)
        #   :pyrope_top:   generated module name to check (implementation side)
        # Defaults: verilog_top = first module in the golden; pyrope_top = the
        # generated module (when unique) or the one matching verilog_top.
        verilog_top = (test.params.get('verilog_top') or '').strip() or self._verilog_top_module(gold_abs)
        if not verilog_top:
            print('{} - equiv - FAILED: no reference top (set :verilog_top: or declare a module in {})'.format(name, gold))
            return 1

        pyrope_top = (test.params.get('pyrope_top') or '').strip()
        if not pyrope_top:
            gen_mods = self._verilog_modules(impl)
            if len(gen_mods) == 1:
                pyrope_top = gen_mods[0]
            elif verilog_top in gen_mods:
                pyrope_top = verilog_top
            else:
                print('{} - equiv - FAILED: {} generated modules {}; set :pyrope_top:'.format(name, len(gen_mods), gen_mods))
                return 1

        # Every equivalence pair uses native Slang and the default LEC solver.
        lec_cmd = [self.lhd, 'lec', '--impl', 'verilog:' + impl, '--ref', 'verilog:' + gold,
                   '--impl-top', pyrope_top, '--ref-top', verilog_top,
                   '--workdir', os.path.join(odir, 'w_lec')] + self.lec_satopt_args() + self._extra_sets(test) + self.equiv_set_args()
        lec = run_lec(lec_cmd, cwd=tmp_dir, timeout=20)
        ltxt = lec.stdout.decode('utf-8', 'ignore')
        # Hierarchical LEC may retain an intermediate collapsed-box UNKNOWN in
        # the successful flat retry's detail. Judge the final top-level line;
        # PASS(n) is a real bounded pass, not an inconclusive result.
        lec_ok = lec_verdict(lec) == 'proven'
        if not lec_ok:
            print('{} - equiv - FAILED: lhd lec did not PROVE (our own corpus must be decidable by '
                  'our own engine; verilog_top:{} pyrope_top:{})'.format(name, verilog_top, pyrope_top))
            print(ltxt)

        if not lec_ok:
            return 1

        # The SECOND, independent opinion on the same pair (`:yosys_lec: false`
        # opts a pair out). See run_yosys_lec.
        if self.run_yosys_lec(test, impl, pyrope_top, gold_abs, verilog_top, odir):
            return 1

        print('{} - equiv - success (lhd lec; verilog_top:{} pyrope_top:{})'.format(
            name, verilog_top, pyrope_top))
        return 0

    # ── the yosys/lgcheck cross-oracle (`lgyosys`) ───────────────────────────
    #
    # `lhd lec` above and `inou/yosys/lgcheck` here answer the SAME question
    # with no shared code: cvc5 over the LGraph the front end built, versus
    # yosys `equiv_simple`/`equiv_induct` + a bounded miter over the two Verilog
    # TEXTS. That independence is the whole point — a native path that ever
    # degenerates into proving nothing (an obligation quietly dropped, a vacuous
    # miter, a top that resolves to an empty def) keeps reporting PROVEN over
    # this entire corpus, and only a second engine can notice.
    #
    # yosys' SAT does not scale past small combinational logic (it is the
    # backend LiveHD is replacing, not a gate), so the oracle is deliberately
    # ONE-SIDED: exit 1 — a real counterexample — is the only verdict that fails
    # a pair. An inconclusive result, a spent budget, or a read the yosys
    # front end cannot do is reported and tolerated; it never claims a proof.
    @staticmethod
    def _lgcheck_tools():
        """(lgcheck, yosys2) absolute paths, or (None, None) if unavailable.

        One relative spelling covers both cwds this harness runs in: the repo
        root and a bazel sh_test's runfiles root (`//inou/yosys:scripts` is a
        `deps` of every prp-equiv-* target, so both files are staged there).
        lgcheck resolves its own yosys and ware/rtl include RELATIVE TO CWD, and
        it is run from the pair's scratch dir, so pass the binary explicitly.
        """
        lgcheck = os.environ.get('LHD_LGCHECK') or 'inou/yosys/lgcheck'
        if not os.access(lgcheck, os.X_OK):
            return None, None
        for cand in ('inou/yosys/yosys2', 'bazel-bin/inou/yosys/yosys2'):
            if os.access(cand, os.X_OK):
                return os.path.abspath(lgcheck), os.path.abspath(cand)
        return os.path.abspath(lgcheck), None

    def run_yosys_lec(self, test, impl, impl_top, gold, gold_top, odir):
        """yosys `equiv` on (generated verilog, golden verilog). 1 only on REFUTED."""
        name = test.params['name']
        if str(test.params.get('yosys_lec', 'true')).strip().lower() in ('false', 'off', '0', 'no'):
            # An opted-out pair states WHY in its header (X-semantics the yosys
            # miter reads differently, a construct read_verilog rejects, ...).
            print('{} - lgyosys - skipped (:yosys_lec: false)'.format(name))
            return 0

        lgcheck, yosys = self._lgcheck_tools()
        if lgcheck is None:
            # Not a silent pass: say it, so a missing oracle is visible in the
            # log of every pair rather than looking like a clean cross-check.
            print('{} - lgyosys - unavailable (no inou/yosys/lgcheck; cross-check NOT run)'.format(name))
            return 0

        # `:yosys_lec_timeout: N` — yosys' own shared equivalence budget. Small
        # by default: a pair this oracle cannot decide quickly it will not decide
        # at all, and the native proof above is the gate that must hold.
        try:
            budget = max(1, int(str(test.params.get('yosys_lec_timeout', 10)).strip()))
        except ValueError:
            print('{} - lgyosys - FAILED: :yosys_lec_timeout: must be an integer'.format(name))
            return 1

        rundir = os.path.join(odir, 'w_lgyosys')
        shutil.rmtree(rundir, ignore_errors=True)
        os.makedirs(rundir, exist_ok=True)
        cmd = [lgcheck, '--implementation', os.path.abspath(impl), '--reference', os.path.abspath(gold),
               '--implementation_top', impl_top, '--reference_top', gold_top]
        if yosys:
            cmd += ['--yosys', yosys]
        env = dict(os.environ, LGCHECK_EQUIV_TIMEOUT=str(budget))
        # lgcheck's own budget covers the yosys strategies only (reading the two
        # sources is outside it, and its portable watchdog adds a 30s kill
        # grace), so keep an outer wall too. Own the process GROUP: the killed
        # lgcheck otherwise leaves its yosys child running.
        proc = subprocess.Popen(cmd, cwd=rundir, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        try:
            out, _ = proc.communicate(timeout=3 * budget)
            rc = proc.returncode
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            out, _ = proc.communicate()
            rc = 124
        text = out.decode('utf-8', 'ignore')

        # lgcheck's exit classes: 0 equivalent, 1 REAL counterexample (its
        # comment: "exit 1 is reserved for a real CEX"), 2 inconclusive (a
        # strategy ran out of budget or the bounded miter found nothing),
        # 5 setup failure (a side yosys could not read, no yosys, ...), 124 a
        # killed run. Only 1 is a disproof.
        if rc == 1:
            print('{} - lgyosys - FAILED: yosys REFUTED the pair the native lec PROVED '
                  '(impl_top:{} ref_top:{}); one of the two engines is wrong'.format(name, impl_top, gold_top))
            print(text)
            # The counterexample itself is in lgcheck's own bounded-miter log.
            bmc = os.path.join(rundir, 'lgcheck_bmc.log')
            if os.path.exists(bmc):
                print('--- {} (tail) ---'.format(bmc))
                with open(bmc) as f:
                    print(''.join(f.readlines()[-40:]))
            return 1

        if rc == 0:
            print('{} - lgyosys - proven (yosys equiv agrees)'.format(name))
            return 0

        # Tolerated, never a proof. `bounded-clean` is the useful middle: the
        # miter WAS built and solved, just only to LGCHECK_BMC_STEPS depth.
        verdict = {2: 'inconclusive', 5: 'setup-failed', 124: 'timeout'}.get(rc, 'exit {}'.format(rc))
        if rc == 2 and 'BMC: found no counterexample' in text:
            verdict = 'inconclusive (bounded-clean)'
        print('{} - lgyosys - {} - TOLERATED: yosys did not decide, and a non-decision is '
              'not a refutation (budget {}s)'.format(name, verdict, budget))
        # Why it did not decide, without the whole yosys transcript.
        for line in text.splitlines():
            if re.match(r'^(WARN|ERROR|FAIL|INCONCLUSIVE|BMC|error|lgcheck):', line):
                print('  ' + line)
        return 0

    def _emit_combined_verilog(self, tmp_dir, cmd, odir, safe_name, side):
        # Run a compile cmd that emits per-module Verilog into odir, then
        # concatenate the generated .v into one file (so LEC sees every
        # submodule of a hierarchical design). Returns (combined_path, log) or
        # (None, log) on failure.
        shutil.rmtree(odir, ignore_errors=True)
        os.makedirs(odir, exist_ok=True)
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log, _ = proc.communicate()
            rc = proc.returncode
        except Exception:
            proc.kill()
            return None, b''
        if rc != 0:
            return None, log
        gen_vs = sorted(glob.glob(os.path.join(odir, '*.v')))
        if not gen_vs:
            return None, log
        combined = os.path.join(odir, 'all_{}_{}.v'.format(safe_name, side))
        with open(combined, 'w') as out:
            for v in gen_vs:
                with open(v) as f:
                    out.write(f.read())
                    out.write('\n')
        return combined, log

    def run_equiv_slang(self, tmp_dir, test: PrpTest):
        # Check the two emitted Verilog sides through native Slang and default LEC.
        name = test.params['name']
        prp  = test.params['files'][0]
        vfile = os.path.splitext(prp)[0] + '.v'
        v_abs = vfile if os.path.isabs(vfile) else os.path.join(tmp_dir, vfile)
        if not os.path.exists(v_abs):
            print('{} - equiv_slang - FAILED: no golden verilog {}'.format(name, vfile))
            return 1
        safe = re.sub(r'\W+', '_', name)

        # reference: .prp -> Verilog
        ref_odir = os.path.join(tmp_dir, 'tmp_eqs_ref_' + safe)
        ref, rlog = self._emit_combined_verilog(tmp_dir, self.lhd_equiv(test, ref_odir), ref_odir, safe, 'ref')
        if ref is None:
            print('{} - equiv_slang - FAILED: prp->verilog pipeline'.format(name))
            print(rlog.decode('utf-8', 'ignore'))
            return 1

        # implementation: golden .v read by the native slang reader -> Verilog
        impl_odir = os.path.join(tmp_dir, 'tmp_eqs_impl_' + safe)
        impl_cmd = [self.lhd, 'compile', '--reader', 'slang', vfile, '--emit-dir', 'verilog:{}/'.format(impl_odir),
                    '--workdir', self._scratch(test, 'equiv_slang')] + self.equiv_set_args()
        impl, ilog = self._emit_combined_verilog(tmp_dir, impl_cmd, impl_odir, safe, 'impl')
        if impl is None:
            print('{} - equiv_slang - FAILED: --reader slang could not lower {}'.format(name, vfile))
            print(ilog.decode('utf-8', 'ignore'))
            return 1

        verilog_top = (test.params.get('verilog_top') or '').strip() or self._verilog_top_module(v_abs)
        pyrope_top  = (test.params.get('pyrope_top') or '').strip()
        if not pyrope_top:
            gen_mods = self._verilog_modules(ref)
            pyrope_top = gen_mods[0] if len(gen_mods) == 1 else verilog_top
        # BOTH sides here are cgen-emitted Verilog (ref = .prp->cgen, impl =
        # golden.v->slang->cgen), whose module names are FLAT — internal graph
        # names are the hierarchical `file.entity`, but Verilog flattens to the
        # entity. A dotted golden/header top (`const_bit_select.top`) must be
        # reduced to that flat entity (`top`) to resolve in the emitted netlists.
        flat = lambda t: t.rsplit('.', 1)[-1] if t and '.' in t else t
        verilog_top = flat(verilog_top)
        pyrope_top  = flat(pyrope_top)

        check = run_lec(
            [self.lhd, 'lec', '--ref', ref, '--impl', impl,
             '--ref-top', pyrope_top, '--impl-top', verilog_top,
             '--workdir', self._scratch(test, 'equiv_slang_lec')] + self.lec_satopt_args() + self.equiv_set_args(),
            cwd=tmp_dir, timeout=20)
        crc, clog = check.returncode, check.stdout
        if lec_verdict(check) == "proven":
            print('{} - equiv_slang - success (verilog_top:{} pyrope_top:{})'.format(name, verilog_top, pyrope_top))
            return 0
        print('{} - equiv_slang - FAILED: lhd lec did not prove equivalence (verilog_top:{} pyrope_top:{})'.format(
            name, verilog_top, pyrope_top))
        print(clog.decode('utf-8', 'ignore'))
        return 1

    @staticmethod
    def _find_marker_lines(test: PrpTest, marker='locate_error_here'):
        # 1-based line numbers of any comment containing `marker` (default
        # `locate_error_here`; warning tests pass `locate_warning_here`).
        lines = []
        for path in test.params['files']:
            try:
                with open(path) as f:
                    for idx, line in enumerate(f, start=1):
                        if marker in line:
                            lines.append(idx)
            except OSError:
                pass
        return lines

    def _comptime_expected_fail_ok(self, test, log, rc):
        # A comptime test with :verifier_fail: N>0 EXPECTS N known-false
        # casserts. The verifier reports each as an emit-only `cassert-false`
        # diagnostic; lgshell ignored emit-only errors, but lhd (by design)
        # fails the run on ANY sink error. Accept the non-zero exit iff the
        # surfaced error is the expected cassert-false — a tally mismatch
        # ("verifier expected ... but saw") or any other pass error surfaces
        # a different message and still fails the test.
        try:
            expected_fail = int(test.params.get('verifier_fail', '0'))
        except ValueError:
            return rc
        if expected_fail <= 0:
            return rc
        for line in reversed(log.decode('utf-8', 'ignore').splitlines()):
            line = line.strip()
            if not line.startswith('{'):
                continue
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            msg = (rec.get('error') or {}).get('message', '')
            if 'cassert is false' in msg:
                return 0
            return rc
        return rc

    def run_verify(self, tmp_dir, test: PrpTest):
        # `:type: verify` — drive `lhd formal verify` on the fixture (design and
        # `formal` blocks live in ONE .prp file) with a persistent --workdir and
        # check the header expectations against the run's machine artifacts:
        #   :top_module: <name>        --top (defaults to 'top')
        #   :verify_bound: N           formal.bound (default 6)
        #   :verify_proven: N          exact PROVEN count in formal_report.json
        #   :verify_refuted: N         exact REFUTED count; N>0 also demands a
        #                              non-zero exit and workdir/simfail_<test>.prp
        #                              (the counterexample testbench)
        #   :verify_replay: fired      the simfail replay re-fired the assert
        #   :verify_replay: no-refire  the replay ran but warned it could not
        #                              reproduce (free initial state)
        wd = self._scratch(test, 'verify')
        os.makedirs(os.path.join(tmp_dir, wd), exist_ok=True)
        cmd = [self.lhd, 'formal', 'verify']
        cmd += test.params['files']
        cmd += ['--top', test.params['top_module'], '--workdir', wd]
        if str(test.params.get('verify_bound', '6')) != '6':
            cmd += ['--set', 'formal.bound={}'.format(test.params['verify_bound'])]
        # The replay is run by the HARNESS below, VCD-less: the built-in
        # simfail_run compiles the VCD writer source, which bazel runfiles do
        # not stage (cc_library data deps carry headers/libs, not .cpp).
        cmd += ['--set', 'formal.simfail_run=false']
        # `:set:` rides here too: without it a `:set: compile.unroll=true` fixture
        # silently verifies the DEFAULT lowering, so a rolled-only regression
        # passes green while its name and header claim the rolled netlist.
        cmd += self._extra_sets(test)

        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            log_b, _ = proc.communicate()
            rc = proc.returncode
        except:
            proc.kill()
            log_b, rc = b'', 1
        log = log_b.decode('utf-8', 'ignore')

        problems = []
        want_refuted = int(test.params.get('verify_refuted', -1))
        want_proven  = int(test.params.get('verify_proven', -1))
        if want_refuted > 0 and rc == 0:
            problems.append('expected a REFUTED run (exit != 0), got rc=0')
        if want_refuted == 0 and rc != 0:
            problems.append('expected a clean run, got rc={}'.format(rc))

        report = os.path.join(tmp_dir, wd, 'formal_report.json')
        if not os.path.exists(report):
            problems.append('missing {} (the agent-loop report must exist on EVERY run)'.format(report))
        else:
            with open(report) as f:
                rep = json.load(f)
            got_proven  = sum(1 for o in rep['obligations'] if o['verdict'] == 'proven')
            got_refuted = sum(1 for o in rep['obligations'] if o['verdict'] == 'refuted')
            if want_proven >= 0 and got_proven != want_proven:
                problems.append('proven count: want {}, got {}'.format(want_proven, got_proven))
            if want_refuted >= 0 and got_refuted != want_refuted:
                problems.append('refuted count: want {}, got {}'.format(want_refuted, got_refuted))

        simfail_tests = []
        if want_refuted > 0:
            simfail_tests = [os.path.join(tmp_dir, wd, name)
                             for name in os.listdir(os.path.join(tmp_dir, wd))
                             if name.startswith('simfail_') and name.endswith('.prp')]
            if len(simfail_tests) != 1:
                problems.append('expected one simfail_<test>.prp, found {}'.format(simfail_tests))

        # Replay oracle: re-simulate the generated counterexample testbench
        # (VCD-less — the embedded assert alone decides). `fired` = the driven
        # trace re-fires the assert (sim exits non-zero); `no-refire` = the
        # replay completes clean, i.e. the witness is NOT reproducible by input
        # driving alone (free initial state — interactively, `lhd formal
        # verify` warns simfail-replay-no-refire on its own VCD replay).
        replay = test.params.get('verify_replay', '')
        if replay:
            tb = simfail_tests[0] if simfail_tests else os.path.join(tmp_dir, wd, 'simfail_missing.prp')
            rcmd = [self.lhd, 'sim', test.params['files'][0], tb, '--workdir', wd + '_sim']
            rproc = subprocess.Popen(rcmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                rlog_b, _ = rproc.communicate()
                rrc = rproc.returncode
            except:
                rproc.kill()
                rlog_b, rrc = b'', -1
            fired = False
            passed = False
            try:
                with open(os.path.join(tmp_dir, wd + '_sim', 'sim', 'sim_tests.json')) as stream:
                    replay_tests = json.load(stream)
                fired = rrc == 11 and any(t.get('status') == 'fail' and t.get('failing_assert')
                                         for t in replay_tests)
                passed = rrc == 0 and bool(replay_tests) and all(t.get('status') == 'pass' for t in replay_tests)
            except (OSError, ValueError, TypeError):
                pass
            if replay == 'fired' and not fired:
                problems.append('the counterexample replay did not report a runtime assertion failure')
                log += '\n---- replay ----\n' + rlog_b.decode('utf-8', 'ignore')
            if replay == 'no-refire' and not passed:
                problems.append('the free-initial-state replay did not complete with passing tests (rc={})'.format(rrc))
                log += '\n---- replay ----\n' + rlog_b.decode('utf-8', 'ignore')

        if problems:
            print('{} - verify - failed'.format(test.params['name']))
            for p in problems:
                print('  ' + p)
            print(log)
            return 1
        print('{} - verify - success'.format(test.params['name']))
        return 0

    @staticmethod
    def _expect_found(got, key):
        """How many instances `key` matches. A trailing `*` is a prefix glob.

        A loop's replicas are named `<base>__li<ordinal>`, so `rca__li*=8` says
        "eight replicas of one source call site" without pinning eight literal
        names — which is the property under test, not the spelling of each one.
        `*` alone stays the whole-design total.
        """
        if key == '*':
            return got.get('*', 0)
        if key.endswith('*'):
            prefix = key[:-1]
            return sum(n for k, n in got.items() if k != '*' and k.startswith(prefix))
        return got.get(key, 0)

    @staticmethod
    def _parse_expect_instances(spec):
        """`:expect_instances: lane=1 rca__li*=8 *=2` -> ({name: count}, err).

        `*` is the total number of Sub (instance) nodes in the design; a name
        ending in `*` is a prefix glob (see `_expect_found`).
        """
        want = {}
        for tok in spec.split():
            if '=' not in tok:
                return None, 'bad :expect_instances: token {!r} (want name=count)'.format(tok)
            k, v = tok.rsplit('=', 1)
            try:
                want[k] = int(v)
            except ValueError:
                return None, 'bad :expect_instances: count in {!r}'.format(tok)
        return want, None

    def _sub_instance_counts(self, tmp_dir, lgdir):
        """Counts Sub nodes per instance name in an emitted `lg:` directory.

        Counted on the LGRAPH, not on emitted Verilog/C++: the code generators
        de-collide repeated instance names differently (`_cgen2` vs `__i2`), so
        a source-level assertion written against either one would encode a
        naming convention instead of the structure under test.

        Returns `(counts, error_text)`; `counts` is None when the query itself
        failed.
        """
        proc = subprocess.Popen([self.lhd, 'tool', 'grep', 'kind=sub', 'lg:' + lgdir],
                                cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out, _ = proc.communicate()
        # A failed query returns no JSONL records, which would otherwise read as
        # "zero instances" and be reported as a structural mismatch ("the loop
        # unrolled") — a message that actively misdirects, since the loop did
        # not unroll and the query never ran.
        if proc.returncode != 0:
            return None, '`lhd tool grep` returned {}:\n{}'.format(proc.returncode, out.decode('utf-8', 'ignore'))
        counts = {'*': 0}
        for line in out.decode('utf-8', 'ignore').splitlines():
            line = line.strip()
            if not line.startswith('{'):
                continue  # diagnostics interleave with the JSONL records
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if rec.get('t') != 'node':
                continue
            inst = rec.get('name') or 'nil'
            counts[inst] = counts.get(inst, 0) + 1
            counts['*'] += 1
        return counts, None

    def check_expect_instances(self, tmp_dir, test):
        """`:expect_instances:` — assert the design's instance COUNT.

        The point is to catch a source `for` loop that silently unrolled: eight
        iterations calling one callee produce eight Sub nodes unrolled and one
        (replicated) Sub node rolled, so a count is the cheapest structural
        witness of which representation the compiler chose.
        """
        spec = test.params.get('expect_instances')
        if spec is None:
            return 0
        name = test.params['name']
        # An `error` fixture passes precisely BECAUSE its compile fails, and a
        # `warning` fixture is a front-end-only tier; lowering either to an lg:
        # would fail by construction and report a structural mismatch that is
        # not one.
        skip = {'error', 'warning'} & set(test.params['type'])
        if skip:
            print('{} - expect_instances - skipped (:type: {})'.format(name, ' '.join(sorted(skip))))
            return 0
        want, err = self._parse_expect_instances(spec)
        if want is None:
            print('{} - expect_instances - failed: {}'.format(name, err))
            return 1

        # A header-only `_<N>` variant means "the base source, my flags" (see
        # prplec.py): lower the BASE sibling under this variant's `:set:`, or
        # the count is taken over an empty file and every name is "found 0".
        src = test.params['files'][0]
        base_src = None
        try:
            import prplec
            if prplec.is_header_only(src if os.path.isabs(src) else os.path.join(tmp_dir, src)):
                stem, _, tail = src[:-len('.prp')].rpartition('_')
                if stem and tail.isdigit():
                    base_src = stem + '.prp'
        except Exception:
            base_src = None
        if base_src is not None:
            saved = test.params['files']
            test.params['files'] = [base_src] + saved[1:]
            cmd = self.lhd_lgraph(test, 'expect')
            test.params['files'] = saved
        else:
            cmd = self.lhd_lgraph(test, 'expect')
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log, _ = proc.communicate()
        if proc.returncode != 0:
            print('{} - expect_instances - failed: lg: lowering returned {}'.format(name, proc.returncode))
            print(log.decode('utf-8', 'ignore'))
            return 1

        got, gerr = self._sub_instance_counts(tmp_dir, self._scratch_path(test, 'expect', '_lg'))
        if got is None:
            print('{} - expect_instances - failed: {}'.format(name, gerr))
            return 1
        bad = [(k, n, self._expect_found(got, k)) for k, n in want.items() if self._expect_found(got, k) != n]
        if bad:
            print('{} - expect_instances - failed (the loop unrolled, or an instance name changed):'.format(name))
            for k, n, g in bad:
                print('    {}: expected {}, found {}'.format(k, n, g))
            print('    all instances found: {}'.format(
                ' '.join('{}={}'.format(k, v) for k, v in sorted(got.items()))))
            return 1
        print('{} - expect_instances - success ({})'.format(name, spec))
        return 0

    # ── state-name correspondence (`pass semdiff --stats`) ────────────────────
    #
    # An equiv pair proves the two designs COMPUTE the same thing. It says
    # nothing about whether they SPELL their state the same way, and that
    # spelling is load-bearing: hierarchical LEC pairs boxes by name, VCD diffs
    # and checkpoints are name-keyed, and a structural pairing degrades to
    # Unknown the moment two flops look alike. So the pair is also diffed
    # structurally and every register/memory must find a counterpart — by NAME,
    # or by STRUCTURE when the fixture sets `:name_match_only: false`.
    #
    # There is NO header tag for "this one does not match". A pair whose state
    # finds no counterpart at all FAILS, and the known-broken ones are carried by
    # the bazel `fixme` tag on their own `prp-statematch-*` target (BUILD's
    # _STATEMATCH_FIXME) — the repo's one convention for "red, and we know it".
    # A per-fixture header tag would have made them green, which is exactly the
    # silence this check exists to break.

    _SEMDIFF_STAT_RE = re.compile(
        r'ref (\d+)/(\d+) paired .*?impl (\d+)/(\d+).*?by name (\d+), by structure (\d+)')

    @staticmethod
    def _resolve_lg_entity(lgdir, want, stem):
        """`want` as this library spells it, or '' when it has no such entity.

        A library.txt lists `graph_io <hash> <entity>`; the entity is normally
        `<file-stem>.<module>` while the header tag carries the bare module name.
        """
        if not want:
            return ''
        want = want.strip()
        names = []
        try:
            with open(os.path.join(lgdir, 'library.txt')) as f:
                for line in f:
                    tok = line.split()
                    if len(tok) >= 3 and tok[0] == 'graph_io':
                        names.append(tok[2])
        except OSError:
            return ''
        for cand in (want, '{}.{}'.format(stem, want)):
            if cand in names:
                return cand
        return ''

    @staticmethod
    def _first_error_message(log):
        """The first `severity=error` message in an lhd JSONL log, or ''."""
        for line in log.decode('utf-8', 'ignore').splitlines():
            line = line.strip()
            if not line.startswith('{'):
                continue
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if rec.get('severity') == 'error':
                return '{} — {}'.format(rec.get('code', '?'), rec.get('message', ''))
        return ''

    def _semdiff_state_stats(self, tmp_dir, test, workdir):
        """Compile both sides of an equiv pair to lg: and diff them.

        Returns ({kind: {...}}, err). `err` non-None means the pair could not be
        measured at all — a side the reader will not lower. That is a FAILURE,
        not a skip: the equiv proof and this state comparison have separate obligations and can
        stay green while `lhd compile` refuses the very same file, which is
        precisely the kind of hole this check exists to expose.
        """
        prp  = test.params['files'][0]
        gold = os.path.splitext(prp)[0] + '.v'
        lg_ref  = os.path.join(workdir, 'ref_lg')
        lg_impl = os.path.join(workdir, 'impl_lg')

        # The Pyrope side is `--ref`: it is the design whose state names this
        # repo controls, so it is the side whose elements must all find a home.
        # No `-q`: the log is captured and only printed on failure, and the
        # quiet flag suppresses the very JSONL diagnostics that say WHY a side
        # would not lower.
        base = [self.lhd, 'compile']
        runs = [(base + [prp] + self._extra_sets(test)
                 + (['--top', test.params['compile_top'].strip()] if test.params.get('compile_top') else [])
                 + (['--set', 'upass.reset_style=' + test.params['reset_style']]
                    if test.params.get('reset_style', 'sync') != 'sync' else [])
                 + ['--emit-dir', 'lg:' + lg_ref, '--workdir', os.path.join(workdir, 'w_ref')]),
                (base + [gold]
                 + ['--emit-dir', 'lg:' + lg_impl, '--workdir', os.path.join(workdir, 'w_impl')])]
        for cmd in runs:
            proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log, _ = proc.communicate()
            if proc.returncode != 0:
                src = next((a for a in cmd if a.endswith('.prp') or a.endswith('.v')), '?')
                why = self._first_error_message(log)
                return None, '`lhd compile {}` failed (rc={}){}'.format(
                    os.path.basename(src), proc.returncode, ': ' + why if why else '')

        # A flat golden can describe state held in generic child instances.
        # Compare occurrence names after flattening both selected tops when the
        # fixture requests it; every state element must still match by name.
        if test.params.get('state_match_flatten', '').strip().lower() in ('true', '1', 'yes', 'on'):
            flat_dirs = []
            stem = os.path.splitext(os.path.basename(prp))[0]
            for side, lgdir, tag in (('ref', lg_ref, 'pyrope_top'), ('impl', lg_impl, 'verilog_top')):
                top = self._resolve_lg_entity(
                    lgdir if os.path.isabs(lgdir) else os.path.join(tmp_dir, lgdir), test.params.get(tag), stem)
                if not top:
                    return None, ':state_match_flatten: requires a resolvable :{}:'.format(tag)
                flat_dir = lgdir + '_flat'
                cmd = [self.lhd, 'pass', 'color', 'flat', 'lg:' + lgdir, '--top', top,
                       '--emit-dir', 'lg:' + flat_dir, '--workdir', os.path.join(workdir, 'w_flat_' + side)]
                proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                log, _ = proc.communicate()
                if proc.returncode != 0:
                    return None, 'flatten {} failed (rc={}): {}'.format(side, proc.returncode, self._first_error_message(log))
                flat_dirs.append(flat_dir)
            lg_ref, lg_impl = flat_dirs

        # Tops are resolved against each library's OWN entity list before being
        # passed. `:pyrope_top:`/`:verilog_top:` name the MODULES LEC
        # compares, which is not always how the lg: entity is spelled (`top` vs
        # the entity `mod_varargs_csa.top`), and handing semdiff a name it has to
        # guess at lands in its `top-entity-fallback` path — which ABORTS on some
        # libraries (`raw_hash_set.h: operator-> called on end() iterator`, seen
        # on generic_mod). An unresolvable top is simply omitted; semdiff's hier
        # sweep then pairs the defs by name on its own.
        cmd = [self.lhd, 'pass', 'semdiff', '--stats', '--ref', 'lg:' + lg_ref, '--impl', 'lg:' + lg_impl,
               '--workdir', os.path.join(workdir, 'w_diff')]
        stem = os.path.splitext(os.path.basename(prp))[0]
        for flag, lgdir, want in (('--ref-top', lg_ref, test.params.get('pyrope_top')),
                                  ('--impl-top', lg_impl, test.params.get('verilog_top'))):
            top = self._resolve_lg_entity(os.path.join(tmp_dir, lgdir) if not os.path.isabs(lgdir) else lgdir,
                                          want, stem)
            if top:
                cmd += [flag, top]
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log, _ = proc.communicate()
        if proc.returncode != 0:
            return None, 'pass semdiff returned {}'.format(proc.returncode)

        got = {}
        for line in log.decode('utf-8', 'ignore').splitlines():
            for kind, tag in (('regs', 'registers'), ('mems', 'memories')):
                if 'semdiff[stats]: ' + tag not in line:
                    continue
                m = self._SEMDIFF_STAT_RE.search(line)
                if m:
                    got[kind] = {'paired': int(m[1]), 'total': int(m[2]),
                                 'impl_paired': int(m[3]), 'impl_total': int(m[4]),
                                 'by_name': int(m[5]), 'by_struct': int(m[6])}
        if not got:
            return None, 'pass semdiff printed no --stats report'
        return got, None

    def check_state_match(self, tmp_dir, test):
        """`:name_match_only:` — state-name correspondence for an equiv pair.

        Every ref-side register and memory must find a counterpart: by NAME, or
        by STRUCTURE when the fixture sets `:name_match_only: false`. Anything
        less FAILS — including a side `lhd compile` will not lower. A design with
        no registers and no memories has nothing to correspond and passes
        without saying anything.
        """
        name = test.params['name']
        prp  = test.params['files'][0]
        gold = os.path.splitext(prp)[0] + '.v'
        if not os.path.exists(gold if os.path.isabs(gold) else os.path.join(tmp_dir, gold)):
            print('{} - state_match - failed: no golden {}'.format(name, gold))
            return 1

        # Default TRUE: name correspondence is the goal, so a pair that only
        # matches structurally has to say so in its header rather than drift
        # there silently.
        name_only = (test.params.get('name_match_only', 'true').strip().lower()
                     not in ('false', '0', 'no', 'off'))

        workdir = os.path.join(tmp_dir, self._scratch(test, 'statematch'))
        os.makedirs(workdir, exist_ok=True)
        got, err = self._semdiff_state_stats(tmp_dir, test, workdir)
        if got is None:
            print('{} - state_match - failed: {}'.format(name, err))
            return 1

        if all(got.get(k, {}).get('total', 0) == 0 for k in ('regs', 'mems')):
            return 0  # combinational both sides — nothing to correspond

        bad = []
        for kind in ('regs', 'mems'):
            st = got.get(kind)
            if st is None or st['total'] == 0:
                continue
            if st['paired'] != st['total']:
                bad.append('{}: only {}/{} ref element(s) found a counterpart in the golden '
                           '(golden has {})'.format(kind, st['paired'], st['total'], st['impl_total']))
            elif name_only and st['by_struct'] != 0:
                bad.append('{}: {} of {} pair(s) matched by STRUCTURE, not by name (set '
                           ':name_match_only: false to accept that)'.format(kind, st['by_struct'], st['total']))
        if bad:
            print('{} - state_match - failed:'.format(name))
            for b in bad:
                print('    ' + b)
            for kind in ('regs', 'mems'):
                if kind in got:
                    print('    measured {}: ref {}/{}, impl {}/{}, by name {}, by structure {}'.format(
                        kind, got[kind]['paired'], got[kind]['total'], got[kind]['impl_paired'],
                        got[kind]['impl_total'], got[kind]['by_name'], got[kind]['by_struct']))
            return 1
        print('{} - state_match - success ({})'.format(
            name, ' '.join('{} {}/{}{}'.format(k, got[k]['paired'], got[k]['total'],
                                               '' if got[k]['by_struct'] == 0 else ' (struct)')
                           for k in ('regs', 'mems') if k in got and got[k]['total'])))
        return 0

    def _run_compile(self, tmp_dir, test, mode, cmd=None, label=None):
        cmd = self.gen_lhd_cmd(test, mode) if cmd is None else cmd
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        log, mode_rc = b'', 1
        try:
            log, _ = proc.communicate()
            mode_rc = proc.returncode
        except BaseException as e:
            proc.kill()
            proc.wait()
            log = 'exception while running {}: {!r}\n'.format(cmd[0], e).encode('utf-8')
        if mode == 'comptime' and mode_rc != 0:
            mode_rc = self._comptime_expected_fail_ok(test, log, mode_rc)
        print('{} - {} - {}'.format(test.params['name'], label or mode,
                                   'success' if mode_rc == 0 else 'failed'))
        if mode_rc != 0:
            print(log.decode('utf-8', 'ignore'))
        return mode_rc

    def run_comptime(self, tmp_dir, test):
        # Check both sources with the SAME verifier counts and pass flags.
        # A formatter success alone says nothing about constant evaluation.
        rc = self._run_compile(tmp_dir, test, 'comptime')
        formatted = copy.deepcopy(test)
        formatted.params['files'] = []
        # abspath: the scandir skip below compares against os.path.abspath(entry.path),
        # so a relative tmp_dir would never match and the scratch dir would be
        # symlinked into itself.
        sources = os.path.abspath(os.path.join(tmp_dir, self._scratch(test, 'fmt_sources')))
        os.makedirs(sources, exist_ok=True)
        parents = {}
        for source in test.params['files']:
            original = os.path.abspath(os.path.join(tmp_dir, source))
            parent = os.path.dirname(original)
            if parent not in parents:
                directory = os.path.join(sources, str(len(parents)))
                os.makedirs(directory)
                parents[parent] = directory
                # Retain sibling imports/resources without modifying them.
                # Selected source links are replaced by formatted files below.
                for entry in os.scandir(parent):
                    if os.path.abspath(entry.path) != sources:
                        os.symlink(entry.path, os.path.join(directory, entry.name))
            output = os.path.join(parents[parent], os.path.basename(original))
            if os.path.lexists(output):
                os.unlink(output)
            cmd = [self.lhd, 'pyrope', 'fmt', original, '--output', output]
            proc = subprocess.run(cmd, cwd=tmp_dir, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
            if proc.returncode != 0 or not os.path.isfile(output):
                print('{} - comptime format - failed'.format(test.params['name']))
                print(proc.stdout.decode('utf-8', 'ignore'))
                return 1
            formatted.params['files'].append(output)
        cmd = self.lhd_comptime(formatted, 'comptime_formatted')
        rc |= self._run_compile(tmp_dir, formatted, 'comptime', cmd=cmd,
                                label='comptime after format')
        return rc

    def run(self, tmp_dir, test: PrpTest):

        # KNOWN GAP (deliberately left as-is): with a multi-mode `:type:` (e.g.
        # `:type: parsing lnast comptime`) each mode ASSIGNS rc, so an earlier
        # mode's failure is overwritten by a later mode's success and the test
        # reports green. Changing these to `rc |=` is a one-line fix, but it
        # currently turns eight pre-existing product failures red — every one of
        # them a multi-mode fixture failing in `parsing`/`lnast`:
        #   assert_ifelse, assert_ifelse2, attributes, enum_types, expressions,
        #   stmt_kinds, string_format_spec, string_integer
        # Fix those first, then switch this to `rc |=`.
        rc = 0
        for mode in test.params['type']:
            if mode == 'comptime':
                rc = self.run_comptime(tmp_dir, test)
                if rc != 0:
                    return rc  # Neither pass may be hidden by a later mode.
                continue
            if mode == 'error':
                rc = self.run_error(tmp_dir, test)
                continue
            if mode == 'warning':
                rc = self.run_warning(tmp_dir, test)
                continue
            if mode == 'equiv':
                rc = self.run_equiv(tmp_dir, test)
                if rc:
                    return rc
                continue
            if mode == 'statematch':
                # Its own `--mode`, driven by its own `prp-statematch-*` target,
                # deliberately NOT a post-check of `equiv`: a pair can be
                # PROVEN equivalent and still spell its state differently, and
                # folding the two together would mean tagging the whole pair
                # `fixme` — dropping a live equivalence proof — every time the
                # naming is the only thing wrong.
                rc = self.check_state_match(tmp_dir, test)
                continue
            if mode == 'lec':
                # PYROPE-vs-PYROPE equivalence: `foo.prp` against its `foo_<N>.prp`
                # variants, discovered from the file name (prplec.py). Lazy import
                # so only the `prp-lec-*` targets stage that module.
                from prplec import run_prplec
                rc = run_prplec(self, tmp_dir, test)
                continue
            if mode == 'equiv_slang':
                rc = self.run_equiv_slang(tmp_dir, test)
                continue
            if mode == 'simulation':
                # The `:type: simulation` flow lives in its own module (prpsim);
                # import lazily so only the `prp-sim-*` targets need it staged.
                from prpsim import run_simulation
                rc = run_simulation(self, tmp_dir, test)
                continue
            if mode == 'vsim':
                # The VERILATOR DIFFERENTIAL for a `tests/sim/` fixture: emit the
                # design's Verilog and run its hand-written C++ twin under
                # verilator. Deliberately a separate mode rather than a step of
                # `simulation`, because the fixtures worth running it on are the
                # ones `lhd sim` REFUSES — the two must be able to disagree.
                from prpvsim import run_verilator_diff
                rc = run_verilator_diff(self, tmp_dir, test)
                continue
            if mode == 'verify':
                rc = self.run_verify(tmp_dir, test)
                continue

            rc = self._run_compile(tmp_dir, test, mode)

        # Structural post-checks, independent of `:type:` so one implementation
        # serves the sim and equiv fixtures alike.
        if rc == 0:
            rc = self.check_expect_instances(tmp_dir, test)

        return rc
