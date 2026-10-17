#!/usr/bin/env python3
"""Acceptance checks for the shared LEC runner, including an actual outer timeout."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from lec import acceptable, DBG_WATCHDOG_SCALE, final_verdict, run_lec, verdict, watchdog_scale


class LecHarness(unittest.TestCase):
    def result(self, code, text):
        return subprocess.CompletedProcess([], code, text.encode())

    def test_only_a_refutation_or_no_verdict_is_a_failure(self):
        # A timeout (lhd's own, exit 0, or the outer watchdog) is never a failure and never a proof.
        self.assertEqual(verdict(self.result(0, "lec: 'top' TIMEOUT-time (formal.timeout reached; neither proved nor refuted) (x)\n")), 'timeout')
        self.assertEqual(verdict(self.result(0, "lec: 'top' TIMEOUT-memory (out of memory; neither proved nor refuted)\n")), 'timeout')
        self.assertEqual(verdict(self.result(124, 'FAIL: LEC outer watchdog exceeded 10s (internal budget 5s)\n')), 'timeout')
        self.assertEqual(verdict(self.result(0, "lec: 'top' TESTED-sampled(2048) (test reached)\n")), 'tested')
        self.assertTrue(acceptable('timeout') and acceptable('proven') and acceptable('tested'))
        # A counterexample is the failure; every other REFUTED-<why> fails too (no answer).
        self.assertEqual(verdict(self.result(10, "lec: 'top' REFUTED-cex (not equivalent)\n")), 'refuted')
        self.assertFalse(acceptable('refuted'))
        for why in ('interface', 'unsupported', 'unchecked', 'vacuous', 'unexplained'):
            status = verdict(self.result(7, "lec: 'top' REFUTED-{} (x)\n".format(why)))
            self.assertEqual(status, 'refuted-' + why)
            self.assertFalse(acceptable(status))
        # A word whose exit code disagrees, or no verdict line at all, is a failure.
        for code, text in [
            (0, 'no obligations found\n'),
            (7, "lec: 'top' REFUTED-cex (not equivalent)\n"),
            (10, "lec: 'top' REFUTED-unsupported (x)\n"),
            (0, "lec: 'child' PROVEN-full (x)\nlec: 'top' REFUTED-unsupported (x)\n"),
            (124, "lec: 'top' TIMEOUT-time (x)\n"),
            (0, "lec: 'top' PROVEN equivalent\n"),  # the old words are not verdicts any more
        ]:
            self.assertEqual(verdict(self.result(code, text)), 'failed', text)
        self.assertEqual(verdict(self.result(0, "lec: 'top' PROVEN-full (x)\n")), 'proven')
        self.assertEqual(verdict(self.result(0, "lec: 'top' PROVEN-bounded(6) (x)\n")), 'proven')  # bounded is a proof

    def test_artifact_messages_do_not_replace_the_verdict(self):
        output = "lec: 'top' REFUTED-cex (not equivalent)\nlec: wrote counterexample waveform /tmp/top.vcd\n"
        self.assertEqual(final_verdict(output), "lec: 'top' REFUTED-cex (not equivalent)")
        self.assertEqual(verdict(self.result(10, output)), 'refuted')
        output = "lec: 'top' PROVEN-full (x)\nlec: wrote report /tmp/report.json\n"
        self.assertEqual(verdict(self.result(0, output)), 'proven')

    def test_option_controls_outer_budget_and_kills_children(self):
        with tempfile.TemporaryDirectory() as work:
            marker = str(Path(work) / 'child-survived')
            child = 'import time; time.sleep(.4); open({!r}, "w").close()'.format(marker)
            parent = ('import subprocess,sys,time; '
                      'subprocess.Popen([sys.executable,"-c",{!r}]); '
                      'print("started",flush=True); time.sleep(10)').format(child)
            started = time.monotonic()
            result = run_lec([sys.executable, '-c', parent, '--set', 'formal.timeout=.1'], timeout=5, scale=1)
            self.assertEqual(result.returncode, 124)
            self.assertIn(b'outer watchdog', result.stdout)
            self.assertLess(time.monotonic() - started, 2)
            time.sleep(.5)
            self.assertFalse(os.path.exists(marker), 'watchdog left its simulator/solver child running')

    def test_watchdog_scales_only_in_unoptimized_trees(self):
        base = '/x/execroot/_main/bazel-out/{}/bin/inou/prp/t.runfiles'
        for mode in ('k8-dbg', 'darwin_arm64-dbg', 'k8-fastbuild', 'darwin_arm64-fastbuild'):
            self.assertEqual(watchdog_scale(base.format(mode)), DBG_WATCHDOG_SCALE)
        for mode in ('k8-opt', 'darwin_arm64-opt'):
            self.assertEqual(watchdog_scale(base.format(mode)), 1)
        self.assertEqual(watchdog_scale('/home/u/dbg-work/livehd'), 1)
        self.assertEqual(watchdog_scale('/home/u/fastbuild-work/livehd'), 1)

    def test_proof_effort_is_the_default_unless_a_test_names_its_own(self):
        probe = 'import sys; print(" ".join(sys.argv[1:]))'
        result = run_lec([sys.executable, '-c', probe], timeout=5, scale=1)
        self.assertIn(b'formal.effort=proof', result.stdout)
        result = run_lec([sys.executable, '-c', probe, '--set', 'formal.effort=verify'], timeout=5, scale=1)
        self.assertIn(b'formal.effort=verify', result.stdout)
        self.assertNotIn(b'formal.effort=proof', result.stdout)

    def test_harness_budget_scales_in_dbg_but_a_pinned_one_does_not(self):
        probe = 'import sys; print(" ".join(sys.argv[1:]))'
        result = run_lec([sys.executable, '-c', probe], timeout=5, scale=1)
        self.assertTrue(result.stdout.strip().endswith(b'formal.timeout=5 --set formal.effort=proof --set formal.simfail_run=false'),
                        result.stdout)
        result = run_lec([sys.executable, '-c', probe], timeout=5, scale=DBG_WATCHDOG_SCALE)
        self.assertIn(b'formal.timeout=15', result.stdout)
        result = run_lec([sys.executable, '-c', probe, '--set', 'formal.timeout=2'], timeout=5, scale=DBG_WATCHDOG_SCALE)
        self.assertNotIn(b'formal.timeout=6', result.stdout)
        self.assertTrue(result.stdout.count(b'formal.timeout=2') == 2, result.stdout)

    def test_unbounded_budget_is_rejected(self):
        for budget in ('0', '-1', 'nan', 'inf'):
            with self.assertRaises(ValueError):
                run_lec([sys.executable, '-c', 'pass', '--set', 'formal.timeout=' + budget])


if __name__ == '__main__':
    unittest.main()
