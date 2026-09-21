#!/usr/bin/env python3
"""Acceptance checks for the shared LEC runner, including an actual outer timeout."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from lec import final_verdict, run_lec, verdict


class LecHarness(unittest.TestCase):
    def result(self, code, text):
        return subprocess.CompletedProcess([], code, text.encode())

    def test_only_internal_timeout_is_accepted_by_sanity_checks(self):
        timeout = self.result(7, 'lec: top UNKNOWN unresolved (hit formal.timeout=5s)\n')
        self.assertEqual(verdict(timeout, sanity=True), 'inconclusive')
        self.assertEqual(verdict(timeout), 'failed')
        for code, text in [
            (7, 'lec: top UNKNOWN unsupported cell\n'),
            (7, 'lec: top UNKNOWN exceeded the 10s hard wall backstop\n'),
            (3, 'lec: top REFUTED\n'),
            (0, 'no obligations found\n'),
            (0, 'lec: child PROVEN equivalent\nlec: top UNKNOWN unsupported\n'),
            (124, 'lec: top UNKNOWN unresolved (hit formal.timeout=5s)\n'),
        ]:
            self.assertEqual(verdict(self.result(code, text), sanity=True), 'failed')
        self.assertEqual(verdict(self.result(0, 'lec: top PROVEN equivalent\n')), 'proven')
        self.assertEqual(verdict(self.result(0, 'lec: top PASS(6) equivalent\n')), 'proven')

    def test_artifact_messages_do_not_replace_the_verdict(self):
        output = "lec: 'top' REFUTED (not equivalent)\nlec: wrote counterexample waveform /tmp/top.vcd\n"
        self.assertEqual(final_verdict(output), "lec: 'top' REFUTED (not equivalent)")
        self.assertEqual(verdict(self.result(10, output), sanity=True), 'failed')
        output = "lec: 'top' PROVEN equivalent\nlec: wrote report /tmp/report.json\n"
        self.assertEqual(verdict(self.result(0, output)), 'proven')

    def test_option_controls_outer_budget_and_kills_children(self):
        with tempfile.TemporaryDirectory() as work:
            marker = str(Path(work) / 'child-survived')
            child = 'import time; time.sleep(.4); open({!r}, "w").close()'.format(marker)
            parent = ('import subprocess,sys,time; '
                      'subprocess.Popen([sys.executable,"-c",{!r}]); '
                      'print("started",flush=True); time.sleep(10)').format(child)
            started = time.monotonic()
            result = run_lec([sys.executable, '-c', parent, '--set', 'formal.timeout=.1'], timeout=5)
            self.assertEqual(result.returncode, 124)
            self.assertIn(b'outer watchdog', result.stdout)
            self.assertLess(time.monotonic() - started, 2)
            time.sleep(.5)
            self.assertFalse(os.path.exists(marker), 'watchdog left its simulator/solver child running')

    def test_unbounded_budget_is_rejected(self):
        for budget in ('0', '-1', 'nan', 'inf'):
            with self.assertRaises(ValueError):
                run_lec([sys.executable, '-c', 'pass', '--set', 'formal.timeout=' + budget])


if __name__ == '__main__':
    unittest.main()
