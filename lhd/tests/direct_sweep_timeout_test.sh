#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A sweep TIMEOUT must leave NOTHING RUNNING.
#
# direct_sweep.py launches `/usr/bin/time ... lean <probe>` and used to bound it
# with `subprocess.run(timeout=...)`, which kills only the process it started --
# the `time` wrapper. Its child `lean` was reparented to init and kept going:
# measured, a leaked probe held 100% CPU and ~8 GB RSS while the sweep moved on,
# so every later module's wall time and RSS were taken on a machine that was
# secretly still busy, and the leak outlived the sweep itself.
#
# So the real property is about DESCENDANTS, and that is what this checks: a
# command whose grandchild outlives it must have no survivors after the timeout.
# Testing only "the call returned" would have passed throughout the bug.
#
# It also pins the other half: `coreet_d2_census.sh` must FORWARD its --timeout
# to the sweep. It parsed the flag, used it for generation, and left phase 2 on
# direct_sweep's own 1800s default -- a run asked for 7200 and was cut at 1800,
# and the row read TIMEOUT as though the model had failed.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
SWEEP=""
for c in "${TEST_SRCDIR:-}/_main/pass/lean/scripts/direct_sweep.py" \
         "$ROOT/pass/lean/scripts/direct_sweep.py" "pass/lean/scripts/direct_sweep.py"; do
  [ -r "$c" ] && { SWEEP="$c"; break; }
done
[ -n "$SWEEP" ] || { echo "FAIL: cannot find direct_sweep.py"; exit 1; }
CENSUS=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_d2_census.sh" \
         "$ROOT/scripts/coreet_d2_census.sh" "scripts/coreet_d2_census.sh"; do
  [ -r "$c" ] && { CENSUS="$c"; break; }
done
[ -n "$CENSUS" ] || { echo "FAIL: cannot find coreet_d2_census.sh"; exit 1; }

if [ -n "${TEST_TMPDIR:-}" ]; then TD="$TEST_TMPDIR/direct_sweep_timeout_runtime"
else TD="$ROOT/generated/direct_sweep_timeout_test/runtime_tmp"; fi
rm -rf "$TD"; mkdir -p "$TD"
rc=0

SWEEP_PY="$SWEEP" PIDFILE="$TD/grandchild.pid" python3 - <<'PYCHK' || rc=1
import importlib.util, os, signal, sys, time

spec = importlib.util.spec_from_file_location("direct_sweep", os.environ["SWEEP_PY"])
ds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ds)

pidfile = os.environ["PIDFILE"]
# A wrapper that outlives nothing and a grandchild that outlives everything:
# the shape of `/usr/bin/time ... lean`, where the wrapper is what a naive
# timeout reaches and the real work is one level down.
cmd = ["bash", "-c", f"sleep 600 & echo $! > {pidfile}; sleep 600"]
t0 = time.time()
out, err, timed_out, rc = ds.run_timed(cmd, timeout=2)
el = time.time() - t0

fails = []
if not timed_out:
    fails.append(f"run_timed did not report a timeout (returned after {el:.1f}s)")
if el > 30:
    fails.append(f"run_timed took {el:.1f}s to give up on a 2s timeout")

# THE assertion: the grandchild must be gone.
for _ in range(50):
    if os.path.exists(pidfile):
        break
    time.sleep(0.1)
if not os.path.exists(pidfile):
    fails.append("the grandchild never recorded its pid, so nothing was tested")
else:
    gpid = int(open(pidfile).read().strip())
    alive = None
    for _ in range(100):           # give the group kill a moment to land
        try:
            os.kill(gpid, 0)
            alive = True
        except ProcessLookupError:
            alive = False
            break
        except PermissionError:
            alive = True
            break
        time.sleep(0.1)
    if alive:
        fails.append(f"grandchild pid {gpid} SURVIVED the timeout; a leaked probe would "
                     f"keep burning CPU and RAM through the rest of the sweep")
        try:
            os.kill(gpid, signal.SIGKILL)   # do not leak it from the test either
        except OSError:
            pass
    else:
        print(f"ok: no descendant survives a sweep timeout "
              f"(os.kill({gpid}, 0) raised ESRCH/ProcessLookupError)")

# ...and the ORDINARY path must report the command's exit status. run_one
# promotes an ACCEPTED verdict to LEAN_ERROR when the probe exited non-zero,
# so a result shape that dropped the status would silently disable that guard
# (it was a NameError once, which at least failed loudly; returning a constant
# 0 would not).
for want in (0, 3):
    o, e, t, rc = ds.run_timed(["bash", "-c", f"echo hi; echo bye >&2; exit {want}"], timeout=30)
    if t:
        fails.append(f"a fast command reported a timeout (exit {want})")
    if rc != want:
        fails.append(f"run_timed returned rc={rc!r} for a command that exited {want}")
    if o.strip() != "hi" or e.strip() != "bye":
        fails.append(f"run_timed lost the command's output (out={o!r} err={e!r})")
if not [f for f in fails if "run_timed returned rc" in f or "lost the command" in f]:
    print("ok: a non-timeout run propagates stdout, stderr and the exit status (0 and 3)")

for f in fails:
    print(f"FAIL: {f}")
sys.exit(1 if fails else 0)
PYCHK

# ---- the census driver forwards its own --timeout --------------------------
if grep -qE 'direct_sweep\.py.*|^.*--timeout "\$TIMEOUT"' "$CENSUS" \
   && grep -A4 'direct_sweep.py' "$CENSUS" | grep -q -- '--timeout "$TIMEOUT"'; then
  echo "ok: coreet_d2_census.sh forwards --timeout to direct_sweep.py"
else
  echo "FAIL: coreet_d2_census.sh does not pass its --timeout to direct_sweep.py,"
  echo "      so phase 2 silently runs under the sweep's own default"
  rc=1
fi

# ...and the default it would otherwise fall back to is still what we think.
DEF="$(grep -oP 'add_argument\("--timeout", type=int, default=\K[0-9]+' "$SWEEP")"
[ -n "$DEF" ] && echo "ok: direct_sweep.py's own default is ${DEF}s (the value a missing forward falls back to)" \
             || { echo "FAIL: direct_sweep.py no longer declares a --timeout default"; rc=1; }

[ "$rc" -eq 0 ] || { echo "FAIL: direct_sweep_timeout_test"; exit 1; }
echo "PASS: direct_sweep_timeout_test"
