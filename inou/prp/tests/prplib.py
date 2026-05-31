#!/usr/bin/env python3

import argparse
import json
import os
import re
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
        if os.path.exists("./bazel-bin/main/lgshell"):
            self.lgshell = "./bazel-bin/main/lgshell"
        elif os.path.exists("./main/lgshell"):
            self.lgshell = "./main/lgshell"
        else:
            print('Failed to find lgshell binary')
            sys.exit(3)

    def lgshell_parse(self, test):
        lg_cmd = []

        lg_cmd.append('inou.prp')
        lg_cmd.append('files:{} parse_only:true'.format(','.join(test.params['files'])))

        return lg_cmd

    def lgshell_lnast(self, test):
        lg_cmd = []

        lg_cmd.append('inou.prp')
        lg_cmd.append('files:{}'.format(','.join(test.params['files'])))
        lg_cmd.append('|> pass.lnastfmt')

        return lg_cmd

    def lgshell_upass(self, test):
        # Pipeline smoke-test: runs constprop only (verifier:false). Exists
        # because constprop has known gaps (tuple index, enum values, string
        # ops, __wrap/__ubits attrs, ...) that would cause the verifier to
        # hard-error on casserts constprop folds incorrectly. These tests
        # just assert the pipeline doesn't crash. For correctness checking,
        # use `:type: comptime`.
        lg_cmd = self.lgshell_lnast(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.upass constprop:1 verifier:false')

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnastfmt')

        return lg_cmd

    def lgshell_comptime(self, test):
        # Pure compile-time program: every cassert must resolve. Default
        # pass.upass pipeline runs the verifier, which hard-errors on
        # known-false cassert and discharges known-true. To opt out for a
        # specific case, drop `:type: comptime` back to `:type: upass`.
        #
        # Optional header tags (read via PrpTest.params):
        #   :verifier_pass: N   — expected count of discharged casserts
        #   :verifier_fail: N   — expected count of known-false casserts
        # When set, the verifier end_run compares its tally and fails the
        # test if they don't match. -1 or absent disables the check.
        lg_cmd = self.lgshell_lnast(test)

        upass_args = 'pass.upass constprop:1'
        if 'verifier_pass' in test.params:
            upass_args += ' verifier_pass:' + test.params['verifier_pass']
        if 'verifier_fail' in test.params:
            upass_args += ' verifier_fail:' + test.params['verifier_fail']
        if 'verifier_include_funcs' in test.params:
            upass_args += ' verifier_include_funcs:' + test.params['verifier_include_funcs']

        lg_cmd.append('|>')
        lg_cmd.append(upass_args)

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnastfmt')

        return lg_cmd

    def lgshell_error(self, test):
        # Expected-failure test: the program must trigger a compile error. The
        # header's :error: / :help: regexes are matched against the emitted
        # diagnostic's message / hint (see run()). Runs the full prp->upass
        # pipeline so an error at any stage (parse, upass) is caught.
        lg_cmd = self.lgshell_lnast(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.upass constprop:1')

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnastfmt')

        return lg_cmd

    def lgshell_lgraph(self, test):
        lg_cmd = self.lgshell_upass(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnastopt')

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnast_tolg')

        return lg_cmd

    def lgshell_lg_compile(self, test):
        lg_cmd = self.lgshell_lgraph(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.cprop')

        lg_cmd.append('|>')
        lg_cmd.append('pass.bitwidth')

        return lg_cmd

    def gen_lgshell_cmd(self, test, mode):
        gen_lg_cmd = {
            'parsing'  : self.lgshell_parse,
            'lnast'    : self.lgshell_lnast,
            'upass'    : self.lgshell_upass,
            'comptime' : self.lgshell_comptime,
            'error'    : self.lgshell_error,
            'lgraph'   : self.lgshell_lgraph,
            'compile'  : self.lgshell_lg_compile
        }

        cmd = []
        cmd.append(self.lgshell)
        cmd.append(' '.join(gen_lg_cmd[mode](test)))

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
        # message/hint match the header :error:/:help: regexes. Diagnostics are
        # read from a JSONL file (LIVEHD_DIAG) — structured + crash-safe, so it
        # survives the dbg abort that a fatal error triggers.
        cmd       = self.gen_lgshell_cmd(test, 'error')
        safe_name = re.sub(r'\W+', '_', test.params['name'])
        diag_path = os.path.join(tmp_dir, 'diag_{}.jsonl'.format(safe_name))
        if os.path.exists(diag_path):
            os.remove(diag_path)

        env = dict(os.environ, LIVEHD_DIAG=diag_path)
        proc = subprocess.Popen(cmd, cwd=tmp_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
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

    @staticmethod
    def _find_marker_lines(test: PrpTest):
        # 1-based line numbers of any comment containing `locate_error_here`.
        marker = 'locate_error_here'
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

    def run(self, tmp_dir, test: PrpTest):

        rc = 0
        for mode in test.params['type']:
            if mode == 'error':
                rc = self.run_error(tmp_dir, test)
                continue

            cmd = []
            if mode == 'simulation':
                pass
            else:
                cmd = self.gen_lgshell_cmd(test, mode)

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

            if rc == 0:
                print('{} - {} - success'.format(test.params['name'], mode))
            else:
                print('{} - {} - failed'.format(test.params['name'], mode))
                print(log.decode('utf-8', 'ignore'))

        return rc
