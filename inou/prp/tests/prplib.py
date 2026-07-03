#!/usr/bin/env python3

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

class PrpTest:
    """
    Pyrope Test Object
    """
    def __init__(self, prp_file):
        # Set default values
        self.params = {}
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

    def __init__(self):
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

    def _scratch(self, test, mode, suffix=''):
        # `tmp*` prefix so the dirs are .gitignore-covered when the harness is
        # run manually from the repo root (they are created under cwd).
        path = 'tmp_lhd_{}_{}{}'.format(self._safe_name(test), mode, suffix)
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
        return cmd

    def lhd_comptime(self, test, mode):
        # Pure compile-time program: every cassert must resolve. The verifier
        # (lhd default: off) is turned ON, mirroring the old bare pass.upass
        # default; it hard-errors on known-false cassert and discharges
        # known-true. To opt out for a specific case, drop `:type: comptime`
        # back to `:type: upass`.
        #
        # Optional header tags (read via PrpTest.params):
        #   :verifier_pass: N   — expected count of discharged casserts
        #   :verifier_fail: N   — expected count of known-false casserts
        # When set, the verifier end_run compares its tally and fails the
        # test if they don't match. -1 or absent disables the check.
        cmd = self.lhd_upass(test, mode)
        cmd += ['--set', 'upass.verifier=true']
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
        # the LNAST->LGraph lowering (recipe O0 + lg: emit) so errors that
        # only fire at tolg (e.g. a func_call with no hardware lowering) are
        # exercisable as error tests.
        cmd = self.lhd_upass(test, mode)
        cmd += ['--set', 'upass.verifier=true']
        if test.params.get('tolg'):
            cmd += ['--recipe', 'O0', '--emit-dir',
                    'lg:{}/'.format(self._scratch(test, mode, '_lg'))]
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
        # lowering (the CLI-level tolg:1); --recipe O0 keeps the graph passes
        # out, matching the old `pass.upass ... tolg:1` pipeline tail.
        cmd = self.lhd_upass(test, mode)
        cmd += ['--recipe', 'O0', '--emit-dir',
                'lg:{}/'.format(self._scratch(test, mode, '_lg'))]
        return cmd

    def lhd_lg_compile(self, test, mode):
        # tolg + pass.cprop + pass.bitwidth == recipe O2 over the lg: emit.
        cmd = self.lhd_upass(test, mode)
        cmd += ['--recipe', 'O2', '--emit-dir',
                'lg:{}/'.format(self._scratch(test, mode, '_lg'))]
        return cmd

    def lhd_equiv(self, test, odir):
        # Equivalence test: lower to LGraph (tolg, no graph passes) and emit
        # per-module Verilog into `odir`. run_equiv() then LECs the generated
        # Verilog against the sibling golden `.v` via inou/yosys/lgcheck.
        #
        # Optional `:reset_style: async` header tag (task 2d-reg): set the
        # upass.reset_style elaboration flag so the implicit-reset flops wire
        # an async reset and the golden can assert the async always-block.
        cmd = self.lhd_upass(test, 'equiv')
        if 'reset_style' in test.params:
            cmd += ['--set', 'upass.reset_style={}'.format(test.params['reset_style'])]
        cmd += ['--recipe', 'O0', '--emit-dir', 'verilog:{}/'.format(odir)]
        return cmd

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
        return re.findall(r'\bmodule\s+\\?([^\s(]+)', text)

    @staticmethod
    def _verilog_top_module(vpath):
        # Name (unescaped) of the first module declared in a verilog file.
        mods = PrpRunner._verilog_modules(vpath)
        return mods[0] if mods else None

    @staticmethod
    def _yosys_slang_plugin(tmp_dir):
        # The yosys-slang plugin (slang.so) for lgcheck's `--gold_reader slang`
        # (goldens with SystemVerilog packed structs / '{...} patterns that
        # read_verilog cannot parse). Probes the bazel runfiles layout (cwd is
        # <runfiles>/_main, the external repo sits beside it) and the repo-root
        # bazel-bin layout (manual runs). Returns an absolute path or None.
        for cand in ('../+http_archive+yosys_slang/slang.so',
                     '../+_repo_rules+yosys_slang/slang.so',
                     'bazel-bin/external/+http_archive+yosys_slang/slang.so',
                     'bazel-bin/external/+_repo_rules+yosys_slang/slang.so'):
            path = os.path.normpath(os.path.join(tmp_dir, cand))
            if os.path.exists(path):
                return path
        return None

    def run_equiv(self, tmp_dir, test: PrpTest):
        # Lower each .prp function to Verilog (inou.prp -> pass.upass tolg:1 ->
        # inou.cgen.verilog) and prove it equivalent to its sibling golden .v
        # via inou/yosys/lgcheck (formal LEC). The generated module names are
        # the function-tree names (e.g. trivial_if.fun3); the golden must
        # declare the same module name so lgcheck --top matches.
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

        lgcheck_cmd = ['./inou/yosys/lgcheck', '--reference', gold, '--implementation', impl,
                       '--reference_top', verilog_top, '--implementation_top', pyrope_top]
        # Optional `:gold_reader: slang` header: read the golden through the
        # yosys-slang plugin instead of read_verilog (goldens using
        # SystemVerilog packed structs / '{...} patterns). The golden's top
        # module must be a PLAIN identifier (no escaped `\a.b ` names — those
        # break yosys-slang's --top/RTLIL naming), hence the `<name>_top`
        # convention on such goldens.
        if (test.params.get('gold_reader') or '').strip() == 'slang':
            plugin = self._yosys_slang_plugin(tmp_dir)
            if not plugin:
                print('{} - equiv - FAILED: :gold_reader: slang but yosys-slang plugin (slang.so) not found'.format(name))
                return 1
            lgcheck_cmd += ['--gold_reader', 'slang', '--slang_plugin', plugin]
        check = subprocess.Popen(
            lgcheck_cmd,
            cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            clog, _ = check.communicate()
            crc = check.returncode
        except Exception:
            check.kill()
            crc, clog = 1, b''

        if crc == 0:
            print('{} - equiv - success (verilog_top:{} pyrope_top:{})'.format(name, verilog_top, pyrope_top))
            return 0
        print('{} - equiv - FAILED: lgcheck not equivalent (verilog_top:{} pyrope_top:{})'.format(name, verilog_top, pyrope_top))
        print(clog.decode('utf-8', 'ignore'))
        return 1

    def _emit_combined_verilog(self, tmp_dir, cmd, odir, safe_name, side):
        # Run a compile cmd that emits per-module Verilog into odir, then
        # concatenate the generated .v into one file (so lgcheck sees every
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
        # equiv variant for a golden .v that yosys-slang's read_slang cannot
        # ingest (e.g. `'{...}` assignment-pattern lvalues on output ports). The
        # standard `equiv` mode has lgcheck read the golden .v directly, which
        # yosys cannot do here. Instead the .v is read by the NATIVE --reader
        # slang into clean cgen Verilog (implementation) and LEC'd against the
        # .prp-generated Verilog (reference). Only --reader slang is exercised
        # (hence the `_slang` suffix); comparison is top-only via lgcheck.
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
        impl_cmd = [self.lhd, 'compile', '--reader', 'slang', vfile, '--recipe', 'O0',
                    '--emit-dir', 'verilog:{}/'.format(impl_odir),
                    '--workdir', self._scratch(test, 'equiv_slang')]
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

        check = subprocess.Popen(
            ['./inou/yosys/lgcheck', '--reference', ref, '--implementation', impl,
             '--reference_top', pyrope_top, '--implementation_top', verilog_top],
            cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            clog, _ = check.communicate()
            crc = check.returncode
        except Exception:
            check.kill()
            crc, clog = 1, b''
        if crc == 0:
            print('{} - equiv_slang - success (verilog_top:{} pyrope_top:{})'.format(name, verilog_top, pyrope_top))
            return 0
        print('{} - equiv_slang - FAILED: lgcheck not equivalent (verilog_top:{} pyrope_top:{})'.format(
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

    def run(self, tmp_dir, test: PrpTest):

        rc = 0
        for mode in test.params['type']:
            if mode == 'error':
                rc = self.run_error(tmp_dir, test)
                continue
            if mode == 'warning':
                rc = self.run_warning(tmp_dir, test)
                continue
            if mode == 'equiv':
                rc = self.run_equiv(tmp_dir, test)
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

            cmd = self.gen_lhd_cmd(test, mode)

            proc = subprocess.Popen(
                cmd,
                cwd=tmp_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )

            try:
                log, _ = proc.communicate()
                rc = proc.returncode
            except:
                proc.kill()

            if mode == 'comptime' and rc != 0:
                rc = self._comptime_expected_fail_ok(test, log, rc)

            if rc == 0:
                print('{} - {} - success'.format(test.params['name'], mode))
            else:
                print('{} - {} - failed'.format(test.params['name'], mode))
                print(log.decode('utf-8', 'ignore'))

        return rc
