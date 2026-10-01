#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The FALSE-PASS guards in pass/lean/scripts/direct_sweep.py.
#
# That sweep decides whether a certificate is ACCEPTED by reading markers the
# probe prints. The verdict line is printed BEFORE execution finishes, so
# without these guards a probe that prints ACCEPTED and then crashes, or never
# reaches RUNDIRECT, or runs fewer cycles than asked, still landed in the TSV
# as a pass. `cycles` was also prefilled from the REQUEST, so the column that
# looked like evidence of execution was just an echo of the argument.
#
# This drives run_one() directly with synthetic probe outputs -- no Lean, no
# certificates -- so each guard is exercised in isolation.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SWEEP=""
for c in "${TEST_SRCDIR:-}/_main/pass/lean/scripts/direct_sweep.py" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/pass/lean/scripts/direct_sweep.py" \
         "pass/lean/scripts/direct_sweep.py"; do
  [ -r "$c" ] && { SWEEP="$c"; break; }
done
[ -n "$SWEEP" ] || { echo "FAIL: cannot find direct_sweep.py"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/direct_sweep_guards"
rm -rf "$T"; mkdir -p "$T"

SWEEP="$SWEEP" OUTDIR="$T" python3 - <<'PY'
import importlib.util, os, subprocess, sys, time, types

spec = importlib.util.spec_from_file_location("ds", os.environ["SWEEP"])
ds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ds)
out = os.environ["OUTDIR"]

CYCLES = 4
# A minimal certificate file so run_one gets past its CERT_RE / NO_CERT guard.
cert = os.path.join(out, "fake_Lgraph.lean")
open(cert, "w").write("def fake_designCert : DesignCert := {}\n")

OK = ("SHAPE sources=1 nodes=1 outputs=1 flops=0 mems=0 inputs=1\n"
      'VERDICT\t"ACCEPTED"\n' "CHECK_MS 1\n" "STEP1_MS 1\n" "RUN_MS 1\n"
      f"RUNDIRECT\tOK\t{CYCLES}\n")

CASES = [
    ("honest ACCEPTED run",                 OK,                                   0, "ACCEPTED"),
    ("ACCEPTED then NONZERO exit",          OK,                                   1, "LEAN_ERROR"),
    ("ACCEPTED but no RUNDIRECT line",      OK.replace(f"RUNDIRECT\tOK\t{CYCLES}\n", ""), 0, "SIM_INCOMPLETE"),
    ("ACCEPTED but no RUN_MS",              OK.replace("RUN_MS 1\n", ""),         0, "SIM_INCOMPLETE"),
    ("ACCEPTED but no STEP1_MS",            OK.replace("STEP1_MS 1\n", ""),       0, "SIM_INCOMPLETE"),
    ("ACCEPTED but ran FEWER cycles",       OK.replace(f"OK\t{CYCLES}", "OK\t1"), 0, "SIM_INCOMPLETE"),
    ("RUNDIRECT REFUSED",                   OK.replace(f"OK\t{CYCLES}", "REFUSED\tbad"), 0, "RUN_REFUSED"),
]

# PATCH WHAT run_one ACTUALLY CALLS.  It used to be `subprocess.run`; it is
# `ds.run_timed` since the timeout rework, and a stale patch target does not
# fail loudly -- it quietly lets the test LAUNCH THE REAL `lean`, which then
# reports whatever it likes while the synthetic case being tested never runs.
real_run_timed = ds.run_timed
fails = 0
for desc, stdout, rc, want in CASES:
    def fake_run_timed(cmd, timeout, cwd=None, env=None, _o=None, _r=None):
        return _o, "0.1 1000\n", False, _r
    ds.run_timed = (lambda o, r: (lambda *a, **k: (o, "0.1 1000\n", False, r)))(stdout, rc)
    try:
        row = ds.run_one(cert, out, CYCLES, 60, dict(os.environ))
    finally:
        ds.run_timed = real_run_timed
    got = row["verdict"]
    if got != want:
        print(f"FAIL: {desc}: expected {want}, got {got} (reason={row.get('reason','')!r})")
        fails += 1
    else:
        print(f"ok: {desc} -> {got}")

# `cycles` must come from the RUN, never from the request.
ds.run_timed = lambda *a, **k: (OK.replace(f"OK\t{CYCLES}", "OK\t2"), "0.1 1000\n", False, 0)
try:
    row = ds.run_one(cert, out, CYCLES, 60, dict(os.environ))
finally:
    ds.run_timed = real_run_timed
if str(row["cycles"]) == str(CYCLES):
    print(f"FAIL: `cycles` still echoes the request ({CYCLES}) instead of what ran (2)")
    fails += 1
else:
    print(f"ok: `cycles` reports what ran ({row['cycles']}), not the request ({CYCLES})")

# A TIMEOUT is a harness limit, not a model verdict, and it must say so.
ds.run_timed = lambda *a, **k: ("", "", True, -9)
try:
    row = ds.run_one(cert, out, CYCLES, 60, dict(os.environ))
finally:
    ds.run_timed = real_run_timed
if row["verdict"] != "TIMEOUT":
    print(f"FAIL: a timed-out probe reported {row['verdict']}, not TIMEOUT"); fails += 1
elif "harness limit" not in row.get("reason", ""):
    print(f"FAIL: the TIMEOUT row does not say it is a harness limit: {row.get('reason','')!r}")
    fails += 1
else:
    print("ok: a timed-out probe reports TIMEOUT and names it a harness limit")

# A STOPPED SWEEP MUST STILL LEAVE THE ROWS THAT FINISHED.
# `<out>` is written once at the end, so an interrupted run used to leave
# nothing however many modules had already run -- a 71-minute ACCEPTED probe
# was lost that way. Rows go to `<out>.partial` as they complete; `<out>`
# appearing WITHOUT a partial beside it is what says the run finished.
import threading

sweep_root = os.path.join(out, "sweeproot")
os.makedirs(sweep_root, exist_ok=True)
for nm in ("aaa", "zzz"):
    open(os.path.join(sweep_root, f"{nm}_Lgraph.lean"), "w").write(
        f"def {nm}_designCert : DesignCert := {{}}\n")

second_may_finish = threading.Event()
real_run_one = ds.run_one

def staged_run_one(path, workdir, cycles, timeout, lean_env, max_rec_depth=0):
    name = os.path.basename(path).replace("_Lgraph.lean", "")
    if name != "aaa":
        second_may_finish.wait(30)
    return {"module": name, "path": path, "verdict": "ACCEPTED", "reason": "",
            "sources": "1", "nodes": "2", "outputs": "1", "flops": "0", "mems": "0",
            "inputs": "1", "check_ms": "1", "step1_ms": "1", "run_ms": "1",
            "cycles": str(CYCLES), "wall_s": "0.1", "rss_kb": "1000"}

tsv = os.path.join(out, "sweep.tsv")
ds.run_one = staged_run_one
rc_box = {}
argv = sys.argv
sys.argv = ["direct_sweep.py", "--out", tsv, "--jobs", "1", "--cycles", str(CYCLES), sweep_root]
th = threading.Thread(target=lambda: rc_box.update(rc=ds.main()), daemon=True)
th.start()
try:
    deadline = time.time() + 15
    partial = tsv + ".partial"
    got = []
    while time.time() < deadline:
        if os.path.exists(partial):
            got = [l for l in open(partial).read().splitlines() if l.strip()]
            if len(got) >= 2:
                break
        time.sleep(0.1)
    if len(got) < 2:
        print(f"FAIL: the first finished row never reached {partial!r} while the sweep ran")
        fails += 1
    elif not got[1].startswith("aaa\t"):
        print(f"FAIL: the partial's first data row is {got[1][:40]!r}, not the module that finished")
        fails += 1
    elif os.path.exists(tsv):
        print("FAIL: the FINAL tsv exists while the sweep is still running; a consumer "
              "would read a half-done sweep as a complete one")
        fails += 1
    else:
        print("ok: a finished row is on disk while the sweep is still running, and the "
              "final tsv is not")
finally:
    second_may_finish.set()
    th.join(30)
    sys.argv = argv
    ds.run_one = real_run_one
if th.is_alive():
    print("FAIL: the sweep did not finish after the second module was released"); fails += 1
elif not os.path.exists(tsv):
    print("FAIL: no final tsv after a completed sweep"); fails += 1
elif os.path.exists(tsv + ".partial"):
    print("FAIL: the partial survived a COMPLETED sweep, so a finished run looks interrupted")
    fails += 1
else:
    print("ok: a completed sweep leaves the final tsv and removes the partial")

# ...and with jobs > 1 a module that FINISHES must reach the partial even while
# an alphabetically-earlier one is still running. Iterating futures in
# SUBMISSION order and calling f.result() blocks on the first module, so a
# short module that finished long ago would still not be on disk -- at jobs=1
# the two orders coincide, which is why that was invisible.
first_may_finish = threading.Event()

def blocking_first_run_one(path, workdir, cycles, timeout, lean_env, max_rec_depth=0):
    name = os.path.basename(path).replace("_Lgraph.lean", "")
    if name == "aaa":
        first_may_finish.wait(30)
    return {"module": name, "path": path, "verdict": "ACCEPTED", "reason": "",
            "sources": "1", "nodes": "2", "outputs": "1", "flops": "0", "mems": "0",
            "inputs": "1", "check_ms": "1", "step1_ms": "1", "run_ms": "1",
            "cycles": str(CYCLES), "wall_s": "0.1", "rss_kb": "1000"}

tsv2 = os.path.join(out, "sweep_j2.tsv")
ds.run_one = blocking_first_run_one
# The final table must be PUBLISHED, not written in place: an interruption
# during the final write would otherwise leave a truncated file that every
# consumer reads as a complete sweep -- the same failure `.partial` exists to
# avoid, one step later. Record the rename and check that nothing appeared at
# the destination before it.
replace_calls = []
real_replace = os.replace

def recording_replace(src, dst, *a, **k):
    replace_calls.append((src, dst, os.path.exists(dst)))
    return real_replace(src, dst, *a, **k)

ds.os.replace = recording_replace
argv = sys.argv
sys.argv = ["direct_sweep.py", "--out", tsv2, "--jobs", "2", "--cycles", str(CYCLES), sweep_root]
th2 = threading.Thread(target=ds.main, daemon=True)
th2.start()
try:
    deadline = time.time() + 15
    rows2 = []
    while time.time() < deadline:
        if os.path.exists(tsv2 + ".partial"):
            rows2 = [l for l in open(tsv2 + ".partial").read().splitlines() if l.strip()]
            if len(rows2) >= 2:
                break
        time.sleep(0.1)
    if len(rows2) < 2:
        print("FAIL: at jobs=2, a module that finished did not reach the partial while an "
              "alphabetically-earlier module was still running")
        fails += 1
    elif not rows2[1].startswith("zzz\t"):
        print(f"FAIL: at jobs=2 the first persisted row is {rows2[1][:40]!r}; expected the "
              f"module that actually finished first")
        fails += 1
    else:
        print("ok: at jobs=2 a finished module is persisted without waiting for an "
              "earlier-submitted one")
finally:
    first_may_finish.set()
    th2.join(30)
    sys.argv = argv
    ds.run_one = real_run_one
    ds.os.replace = real_replace

pub = [c for c in replace_calls if c[1] == tsv2]
if not pub:
    print("FAIL: the final tsv was written in place, not published by rename; an "
          "interruption during that write leaves a truncated 'complete' sweep")
    fails += 1
elif not pub[0][0].endswith(".tmp"):
    print(f"FAIL: the final tsv was renamed from {pub[0][0]!r}, not a sibling temp")
    fails += 1
elif pub[0][2]:
    print("FAIL: the destination already existed when the rename ran, so a partially "
          "written file was visible under the final name")
    fails += 1
else:
    print("ok: the final tsv is published by renaming a sibling temp over it")

sys.exit(1 if fails else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL: direct_sweep false-pass guards are not holding"; exit 1; }

# ---------------------------------------------------------------------------
# The TSV JOIN must read fields BY NAME.
#
# `while IFS=$'\t' read -r module verdict reason ...` is wrong for this file:
# bash treats tab as IFS *whitespace*, so a run of delimiters collapses and an
# EMPTY field vanishes. `reason` is empty on every successful row, so every
# later column shifted left and the table printed step1_ms as the checkDesign
# time and wall_s as the cycle count -- `accepts(94ms) ran-6.9cyc` for a row
# whose real values were check_ms=48, cycles=4, wall_s=6.9. The evidence was
# correct; the join misreported it.
# ---------------------------------------------------------------------------
JOIN=""
for c in "${TEST_SRCDIR:-}/_main/scripts/lean_validate_join.py" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/lean_validate_join.py" \
         "scripts/lean_validate_join.py"; do
  [ -r "$c" ] && { JOIN="$c"; break; }
done
[ -n "$JOIN" ] || { echo "FAIL: cannot find lean_validate_join.py"; exit 1; }

printf 'module\tverdict\treason\tsources\tnodes\toutputs\tflops\tmems\tinputs\tcheck_ms\tstep1_ms\trun_ms\tcycles\twall_s\trss_kb\tpath\n' > "$T/sweep.tsv"
# EMPTY reason -- the exact shape that broke the bash parse.
printf 'txfma_f0\tACCEPTED\t\t314\t371\t3\t0\t0\t23\t48\t94\t260\t4\t6.9\t547068\t/x/txfma_f0_Lgraph.lean\n' >> "$T/sweep.tsv"
cat > "$T/facts.json" <<'JSON'
{"txfma_f0": {"emit":"0","gates":"0","tc":"0","thm":"elaborated","diffsim":"smoke-pass","ax":"native_decide(trusted)"}}
JSON
out="$(python3 "$JOIN" --tsv "$T/sweep.tsv" --facts "$T/facts.json" --requested txfma_f0 --generated txfma_f0 --sweep-rc 0 2>&1)"; jrc=$?
echo "$out" | sed 's/^/    /'
[ "$jrc" -eq 0 ] || { echo "FAIL: join rejected a well-formed row"; exit 1; }
echo "$out" | grep -q "accepts(48ms)" || { echo "FAIL: CHECKDESIGN must show check_ms=48, not a shifted column"; exit 1; }
echo "$out" | grep -q "ran-4cyc"      || { echo "FAIL: DIRECT_SIM must show cycles=4, not wall_s"; exit 1; }
echo "$out" | grep -q "6.9"           && { echo "FAIL: wall_s leaked into the table as a cycle count"; exit 1; }
echo "ok: empty reason column does not shift check_ms/cycles"

# ---------------------------------------------------------------------------
# FAIL CLOSED. The join is the acceptance gate, so every way a module can be
# bad must make it exit nonzero -- including ways that leave the SWEEP perfectly
# healthy. An earlier version checked only the sweep's rc and row set, so a
# module that failed phase A was simply absent from the expected set and the
# validator still exited 0; with every module failing, nothing was swept, the
# expected set was empty, and the run "succeeded" having validated nothing.
# ---------------------------------------------------------------------------
mkfacts() {  # <file> <emit> <gates> <tc> <thm>
  cat > "$1" <<JSON
{"txfma_f0": {"emit":"$2","gates":"$3","tc":"$4","thm":"$5","diffsim":"prior-smoke","ax":"native_decide(trusted)"}}
JSON
}
expect_fail() {  # <desc> <args...>
  local desc="$1"; shift
  if python3 "$JOIN" "$@" >/dev/null 2>&1; then
    echo "FAIL: $desc -- the gate returned success"; exit 1
  fi
  echo "ok: $desc rejected"
}

expect_fail "nonzero direct_sweep rc" \
  --tsv "$T/sweep.tsv" --facts "$T/facts.json" --requested txfma_f0 --generated txfma_f0 --sweep-rc 2
expect_fail "a requested module missing from the sweep" \
  --tsv "$T/sweep.tsv" --facts "$T/facts.json" --requested txfma_f0,txfma_f9 --generated txfma_f0,txfma_f9

mkfacts "$T/f_emit.json"  1 0 0 elaborated
expect_fail "lean emit nonzero (sweep healthy)" \
  --tsv "$T/sweep.tsv" --facts "$T/f_emit.json" --requested txfma_f0 --generated txfma_f0
mkfacts "$T/f_gates.json" 0 1 0 elaborated
expect_fail "static gates nonzero" \
  --tsv "$T/sweep.tsv" --facts "$T/f_gates.json" --requested txfma_f0 --generated txfma_f0
mkfacts "$T/f_tc.json"    0 0 1 elaborated
expect_fail "typecheck nonzero" \
  --tsv "$T/sweep.tsv" --facts "$T/f_tc.json" --requested txfma_f0 --generated txfma_f0
mkfacts "$T/f_thm.json"   0 0 0 "PROBE-FAIL(rc=1)"
expect_fail "compiles theorem PROBE-FAIL" \
  --tsv "$T/sweep.tsv" --facts "$T/f_thm.json" --requested txfma_f0 --generated txfma_f0

# The case that used to pass vacuously: phase A failed for everything, so
# nothing was generated and nothing was swept.
expect_fail "every module failed phase A, nothing swept" \
  --facts "$T/facts.json" --requested txfma_f0 --generated "" --sweep-rc 0
expect_fail "one of two failed phase A, sweep covers only the survivor" \
  --tsv "$T/sweep.tsv" --facts "$T/facts.json" --requested txfma_f0,txfma_f9 --generated txfma_f0

# Short run with an otherwise perfect row.
sed 's/\t4\t6.9\t/\t1\t6.9\t/' "$T/sweep.tsv" > "$T/short.tsv"
expect_fail "fewer cycles than requested" \
  --tsv "$T/short.tsv" --facts "$T/facts.json" --requested txfma_f0 --generated txfma_f0 --cycles 4

expect_fail "duplicate requested module" \
  --tsv "$T/sweep.tsv" --facts "$T/facts.json" --requested txfma_f0,txfma_f0 --generated txfma_f0

echo "PASS: direct_sweep_guards_test"
