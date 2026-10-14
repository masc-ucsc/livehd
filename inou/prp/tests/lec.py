#!/usr/bin/env python3
"""Shared LEC budgets: an internal timeout may be inconclusive; a watchdog may not."""
import argparse
import math
import os
import re
import signal
import subprocess
import sys


# `-c dbg` builds lhd (and yosys/abc) at -O0: everything LiveHD-side runs ~9x
# slower while the prebuilt cvc5 does not, so formal.timeout (solve time) holds
# but the outer watchdog -- which also covers parse/upass/satopt -- must scale.
DBG_WATCHDOG_SCALE = 10

# Top-level verdict words of `lhd lec` / `lhd formal verify` (pass/lec/property_types.hpp
# Outcome). Every verdict carries a modifier; there is no `UNKNOWN`.
#   PROVEN-full, PROVEN-bounded(n), TESTED-sampled(N)   exit 0
#   TIMEOUT-time, TIMEOUT-memory                         exit 0 (+ warning): not a failure, never a proof
#   REFUTED-cex                                          exit 10: a real counterexample
#   REFUTED-interface|unsupported|unchecked|vacuous|unexplained   exit 7: no answer, and a failure
VERDICT = (r'(?:PROVEN-full|PROVEN-bounded\(\d+\)|TESTED-sampled\(\d+\)|TIMEOUT-(?:time|memory)'
           r'|REFUTED-(?:cex|interface|unsupported|unchecked|vacuous|unexplained))')


def watchdog_scale(path=None):
    """Outer-watchdog multiplier: DBG_WATCHDOG_SCALE when running from a bazel dbg tree."""
    if path is None:
        path = os.environ.get('TEST_SRCDIR') or os.getcwd()
    return DBG_WATCHDOG_SCALE if re.search(r'/bazel-out/[^/]*-dbg/', path) else 1


def run_lec(cmd, cwd=None, timeout=5, scale=None):
    """Return CompletedProcess with bytes output; kill the whole owned group on overrun."""
    for i, arg in enumerate(cmd):
        if arg == '--set' and i + 1 < len(cmd) and cmd[i + 1].startswith('formal.timeout='):
            timeout = float(cmd[i + 1].split('=', 1)[1])
    if not 0 < timeout < float('inf'):
        raise ValueError('integration LEC requires a finite, positive formal.timeout')
    # pass.lec parses formal.timeout with str_tools::to_i (std::from_chars stops
    # at the '.'), and formal.timeout=0 means UNBOUNDED. Emitting '0.5' would
    # therefore remove the budget entirely -- the opposite of the check above.
    # Send whole seconds to the engine; keep the float for the outer watchdog.
    cmd = list(cmd) + ['--set', 'formal.timeout={}'.format(max(1, math.ceil(timeout)))]
    # Equivalence gates need a PROOF, not a sampled TESTED verdict: the default
    # effort (test) samples input vectors first and reports `tested`, which these
    # gates reject. `proof` runs only the proof engines, with formal.timeout as
    # the limit -- the same "try as hard as you can up to the timeout" setting
    # lhdtrack uses against yosys. A test that names its own effort keeps it.
    if not any(arg.startswith('formal.effort=') for arg in cmd):
        cmd += ['--set', 'formal.effort=proof']
    # These gates read only the verdict. A REFUTED run otherwise compiles and
    # runs `lhd sim` to write the counterexample VCD (seconds, against a
    # millisecond solve); the counterexample line itself is still printed, and
    # a REFUTED output says how to get the VCD back.
    no_replay = not any(arg.startswith('formal.simfail_run=') for arg in cmd)
    if no_replay:
        cmd += ['--set', 'formal.simfail_run=false']
    watchdog = 2 * timeout * (watchdog_scale() if scale is None else scale)
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=watchdog)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The process group exited between the deadline and kill.
        out, _ = proc.communicate()
        out += ('\nFAIL: LEC outer watchdog exceeded {:g}s (internal budget {:g}s)\n'
                .format(watchdog, timeout)).encode()
        return subprocess.CompletedProcess(cmd, 124, out)
    if no_replay and b' REFUTED-cex' in out:
        out += (b'NOTE: lec.py skipped the counterexample VCD replay; add --set formal.simfail_run=true '
                b'to the lhd lec command to write it\n')
    return subprocess.CompletedProcess(cmd, proc.returncode, out)


def final_verdict(text):
    """Ignore progress/artifact lines printed after the actual verdict."""
    lines = re.findall(r'^lec: .* ' + VERDICT + r'(?![\w-])[^\n]*', text, re.M)
    return lines[-1] if lines else ''


def verdict_word(text):
    """The verdict word of the final `lec:` line ('' when there is none)."""
    m = re.search(r' (' + VERDICT + r')(?![\w-])', final_verdict(text))
    return m.group(1) if m else ''


def verdict(result, sanity=False):
    """Classify a lhd lec run.

    proven     PROVEN-full or PROVEN-bounded(n): a bounded proof is still a proof
    tested     TESTED-sampled(N) (only when a caller picks a sampled effort): not a failure
    timeout    TIMEOUT-time / TIMEOUT-memory (or the outer watchdog): not a failure, never a proof
    refuted    REFUTED-cex: a real counterexample -- a test failure
    refuted-<why>  REFUTED-interface/-unsupported/-unchecked/-vacuous/-unexplained: no answer, a failure
    failed     no verdict at all (crash, setup error)
    `sanity` is accepted for old callers and ignored: it no longer changes anything.
    """
    del sanity
    text = result.stdout.decode('utf-8', 'replace')
    word = verdict_word(text)
    rc = result.returncode
    if rc == 124 and 'LEC outer watchdog exceeded' in text:
        return 'timeout'
    if rc == 0 and word.startswith('PROVEN-'):
        return 'proven'
    if rc == 0 and word.startswith('TESTED-'):
        return 'tested'
    if rc == 0 and word.startswith('TIMEOUT-'):
        return 'timeout'
    if rc == 10 and word == 'REFUTED-cex':
        return 'refuted'
    if rc == 7 and word.startswith('REFUTED-') and word != 'REFUTED-cex':
        return 'refuted-' + word.split('-', 1)[1]
    return 'failed'


def acceptable(status):
    """Only a refutation (of any kind) or no verdict at all fails a test."""
    return status in ('proven', 'tested', 'timeout')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanity', action='store_true')
    ap.add_argument('--timeout', type=float, default=5)
    ap.add_argument('command', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not cmd:
        ap.error('supply the lhd lec command after --')
    result = run_lec(cmd, timeout=args.timeout)
    sys.stdout.write(result.stdout.decode('utf-8', 'replace'))
    status = verdict(result, args.sanity)
    print('LEC check: ' + status)
    return 0 if acceptable(status) else 1


if __name__ == '__main__':
    sys.exit(main())
