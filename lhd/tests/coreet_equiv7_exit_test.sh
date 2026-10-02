#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# scripts/coreet_equiv7.sh must EXIT NONZERO when a module it was asked about
# fails.
#
# WHY.  It used to end on a `printf`, so its status was the status of printing
# the table -- always 0.  A run whose differential returned 2 (NOT-MEASURED,
# a side failed to build) reported success to its caller, and that is exactly
# how a failed gate reached a report inside an otherwise clean phase.  It also
# decided the emit step had worked by testing `-r impl_$TOP.v`, so a stale file
# from an earlier run would mask a failed emit.
#
# Stubs stand in for the runner, lhd and the differential (the RUNNER/DIFFSIM/
# LHD env seams exist for this), so the test needs no CORE-ET, yosys or solver
# and can drive each failure mode exactly.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
EQ=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_equiv7.sh" \
         "$ROOT/scripts/coreet_equiv7.sh" "scripts/coreet_equiv7.sh"; do
  [ -r "$c" ] && { EQ="$c"; break; }
done
[ -n "$EQ" ] || { echo "FAIL: cannot find coreet_equiv7.sh"; exit 1; }

T="${TEST_TMPDIR:-$ROOT/generated/tests}/coreet_equiv7_exit"
rm -rf "$T"; mkdir -p "$T/bin"
rc_all=0
fail() { echo "FAIL: $*"; rc_all=1; }

# --- stubs ----------------------------------------------------------------
# The runner writes the three markers equiv7 greps for, into $OUT/runner.log,
# and exits with $STUB_RUNNER_RC.
cat > "$T/bin/runner.sh" <<'EOF'
#!/bin/bash
{ echo "compile exit=${STUB_COMPILE:-0}"
  echo "single_edge exit=${STUB_SE:-0}"
  echo "lean emit exit=${STUB_CERT:-0}"; } > /dev/stdout
exit "${STUB_RUNNER_RC:-0}"
EOF
# lhd: only `compile ... --emit verilog:<path>` is used here.  It creates the
# file unless STUB_EMIT_RC says it failed -- and in the "stale file" case it
# creates it AND fails, which is the shape that used to be misread as success.
cat > "$T/bin/lhd.sh" <<'EOF'
#!/bin/bash
out=""
for a in "$@"; do case "$a" in verilog:*) out="${a#verilog:}";; esac; done
[ -n "$out" ] && printf 'module stub; endmodule\n' > "$out"
exit "${STUB_EMIT_RC:-0}"
EOF
cat > "$T/bin/diffsim.py" <<'EOF'
#!/usr/bin/env python3
import os, sys
print("VERDICT: " + os.environ.get("STUB_VERDICT", "DIFFSIM-PASS seed=1 vectors=10"))
sys.exit(int(os.environ.get("STUB_DIFF_RC", "0")))
EOF
chmod +x "$T/bin/runner.sh" "$T/bin/lhd.sh" "$T/bin/diffsim.py"

run_eq() {  # -> sets OUT_TXT / RC ; args are module names
  OUT_TXT="$(OUTROOT="$T/out" RUNNER="$T/bin/runner.sh" LHD="$T/bin/lhd.sh" \
             DIFFSIM="$T/bin/diffsim.py" VECTORS=4 bash "$EQ" "$@" 2>&1)"
  RC=$?
}

# A nonzero exit only means what this test claims if equiv7 actually got as far
# as its per-module loop.  Without this, a script that died on a missing input
# would satisfy every "must be nonzero" case for the wrong reason -- which is
# exactly what happened the first time this ran under bazel, where
# inou/yosys/inou_yosys_read.ys was not in runfiles.
reached_loop() {
  echo "$OUT_TXT" | grep -q "^MODULE  " \
    || { echo "FAIL: $1 -- equiv7 never printed its table, so its nonzero exit"
         echo "      says nothing about gate propagation:"
         echo "$OUT_TXT" | sed 's/^/        /'
         return 1; }
  return 0
}

echo "--- 1. everything passes -> exit 0, and the table still prints ---"
( export STUB_COMPILE=0 STUB_SE=0 STUB_CERT=0 STUB_EMIT_RC=0 STUB_DIFF_RC=0
  run_eq modA
  echo "$OUT_TXT" | sed 's/^/    /'
  [ "$RC" -eq 0 ] || { echo "FAIL: a fully passing run exited $RC"; exit 1; }
  echo "$OUT_TXT" | grep -q "^MODULE " || { echo "FAIL: the header row is gone"; exit 1; }
  echo "$OUT_TXT" | grep -q "modA .*CERT-EMITTED .*DIFFSIM-PASS" \
    || { echo "FAIL: the per-module summary is gone"; exit 1; }
  echo "ok: exit 0, summary preserved" ) || rc_all=1

echo "--- 2. THE BUG: differential returns 2 -> must be nonzero ---"
( export STUB_DIFF_RC=2 STUB_VERDICT="NOT-MEASURED (a side failed to build or run)"
  run_eq modA
  echo "$OUT_TXT" | sed 's/^/    /'
  reached_loop "case 2" || exit 1
  [ "$RC" -ne 0 ] || { echo "FAIL: a NOT-MEASURED differential still exited 0"; exit 1; }
  echo "$OUT_TXT" | grep -q "NOT-MEASURED" || { echo "FAIL: the verdict is not in the table"; exit 1; }
  echo "ok: exit $RC" ) || rc_all=1

echo "--- 3. differential MISMATCH (rc=1) -> nonzero ---"
( export STUB_DIFF_RC=1 STUB_VERDICT="DIFFSIM-MISMATCH seed=1 vectors=10 mismatches=3"
  run_eq modA
  reached_loop "case 3" || exit 1
  echo "$OUT_TXT" | grep -q "DIFFSIM-MISMATCH" || { echo "FAIL: the verdict is not in the table"; exit 1; }
  [ "$RC" -ne 0 ] || { echo "FAIL: a MISMATCH exited 0"; exit 1; }
  echo "ok: exit $RC" ) || rc_all=1

echo "--- 4. candidate emission fails WHILE leaving the file behind ---"
# STATUS, not existence: the stub writes impl_modA.v and then exits 3.
( export STUB_EMIT_RC=3
  run_eq modA
  echo "$OUT_TXT" | sed 's/^/    /'
  reached_loop "case 4" || exit 1
  [ "$RC" -ne 0 ] || { echo "FAIL: a failed emit that left a file behind exited 0"; exit 1; }
  echo "$OUT_TXT" | grep -q "emit-failed(rc=3)" \
    || { echo "FAIL: the emit failure is not named in the table"; exit 1; }
  [ -r "$T/out/modA/impl_modA.v" ] \
    || { echo "FAIL: the stub did not leave the file, so the case proves nothing"; exit 1; }
  echo "ok: exit $RC despite the file existing" ) || rc_all=1

echo "--- 5. generation fails (no certificate) -> nonzero, differential skipped ---"
( export STUB_CERT=1 STUB_RUNNER_RC=1
  run_eq modA
  echo "$OUT_TXT" | sed 's/^/    /'
  reached_loop "case 5" || exit 1
  [ "$RC" -ne 0 ] || { echo "FAIL: a module with no certificate exited 0"; exit 1; }
  echo "$OUT_TXT" | grep -q "skipped" || { echo "FAIL: the differential should be skipped"; exit 1; }
  echo "ok: exit $RC" ) || rc_all=1

echo "--- 6. one good module and one bad -> nonzero, BOTH rows printed ---"
( cat > "$T/bin/runner2.sh" <<'EOF'
#!/bin/bash
cert=0; [ "$COREET_TOP" = "modBad" ] && cert=1
{ echo "compile exit=0"; echo "single_edge exit=0"; echo "lean emit exit=$cert"; }
exit "$cert"
EOF
  chmod +x "$T/bin/runner2.sh"
  OUT_TXT="$(OUTROOT="$T/out2" RUNNER="$T/bin/runner2.sh" LHD="$T/bin/lhd.sh" \
             DIFFSIM="$T/bin/diffsim.py" VECTORS=4 bash "$EQ" modGood modBad 2>&1)"; RC=$?
  echo "$OUT_TXT" | sed 's/^/    /'
  reached_loop "case 6" || exit 1
  [ "$RC" -ne 0 ] || { echo "FAIL: a sweep containing a failure exited 0"; exit 1; }
  echo "$OUT_TXT" | grep -q "modGood .*CERT-EMITTED" || { echo "FAIL: the good row is missing"; exit 1; }
  echo "$OUT_TXT" | grep -q "modBad" || { echo "FAIL: the bad row is missing"; exit 1; }
  echo "ok: exit $RC, both rows present" ) || rc_all=1

[ "$rc_all" -eq 0 ] || { echo "FAIL: coreet_equiv7_exit_test"; exit 1; }
echo "PASS: coreet_equiv7_exit_test (6 of 6 cases EXECUTED)"
