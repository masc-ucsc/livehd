#!/usr/bin/env python3
"""Shared LEC budgets: an internal timeout may be inconclusive; a watchdog may not."""
import argparse
import math
import os
import re
import signal
import subprocess
import sys


def run_lec(cmd, cwd=None, timeout=5):
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
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=2 * timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The process group exited between the deadline and kill.
        out, _ = proc.communicate()
        out += ('\nFAIL: LEC outer watchdog exceeded {:g}s (internal budget {:g}s)\n'
                .format(2 * timeout, timeout)).encode()
        return subprocess.CompletedProcess(cmd, 124, out)
    return subprocess.CompletedProcess(cmd, proc.returncode, out)


def final_verdict(text):
    """Ignore progress/artifact lines printed after the actual verdict."""
    lines = re.findall(r'^lec: .* (?:PROVEN|REFUTED|UNKNOWN|PASS\(\d+\))[^\n]*', text, re.M)
    return lines[-1] if lines else ''


def verdict(result, sanity=False):
    """Validate the final verdict, never equating arbitrary UNKNOWN or exit 0 with proof."""
    text = result.stdout.decode('utf-8', 'replace')
    last = final_verdict(text)
    if result.returncode == 0 and re.search(r' (?:PROVEN|PASS\(\d+\)) equivalent', last):
        return 'proven'
    if (sanity and result.returncode == 7 and ' UNKNOWN ' in last
            and re.search(r'\(hit formal\.timeout=', last)):
        return 'inconclusive'
    return 'failed'


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
    return 1 if status == 'failed' else 0


if __name__ == '__main__':
    sys.exit(main())
