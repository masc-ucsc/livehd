#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# scripts/coreet_d2_census_report.py -- the census's accept/reject logic.
#
# Two distinctions this pins, both of which were wrong at first:
#
#  * direct_sweep exits 1 when it COMPLETED but some certificate was REFUSED.
#    Over 122 modules that is near-certain, and treating rc!=0 as
#    infrastructure failure marked every good row UNTRUSTED because one row
#    refused. rc=1 is "complete, some refused" ONLY when the row set is exactly
#    the set handed to the sweep; rc>=2, or a missing/extra/duplicate row, is
#    infrastructure failure and taints everything.
#  * `accepted` requires EVERY generation gate to be 0 as well. PRESENT was
#    once computed from certificate existence alone, so a module whose static
#    gate had failed could still be swept and reported ACCEPTED.
#
# Builds synthetic run directories; needs no lhd, Lean or CORE-ET.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REP=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_d2_census_report.py" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/coreet_d2_census_report.py" \
         "scripts/coreet_d2_census_report.py"; do
  [ -r "$c" ] && { REP="$c"; break; }
done
[ -n "$REP" ] || { echo "FAIL: cannot find coreet_d2_census_report.py"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/census_report"
rm -rf "$T"; mkdir -p "$T"
fails=0

TSV_HDR='module\tverdict\treason\tsources\tnodes\toutputs\tflops\tmems\tinputs\tcheck_ms\tstep1_ms\trun_ms\tcycles\twall_s\trss_kb\tpath'

# mkmod <dir> <module> <compile> <single_edge> <emit> <gates>
mkmod() {
  local d="$1/mod/$2"; mkdir -p "$d/lean" "$1/logs"
  printf 'ifx\t0\n' > "$d/status"
  echo "x" > "$d/lean/$2_Lgraph.lean"
  { echo "compile exit=$3"; echo "single_edge exit=$4  note"
    echo "lean emit exit=$5"; echo "static gates: gate_status=$6"; } > "$1/logs/$2.log"
}
stage_of() {  # <tsv> <module>
  python3 - "$1" "$2" <<'PY'
import csv, sys
for r in csv.DictReader(open(sys.argv[1]), delimiter='\t'):
    if r['module'] == sys.argv[2]:
        print(r['stage']); break
PY
}
check() {  # <desc> <tsv> <module> <want-stage>
  local got; got="$(stage_of "$2" "$3")"
  if [ "$got" != "$4" ]; then
    echo "FAIL: $1 -- $3 stage is '$got', expected '$4'"; fails=$((fails+1))
  else echo "ok: $1 ($3 -> $got)"; fi
}

# --- 1. one accepted + one REFUSED, sweep rc=1 (complete with refusals) ----
D="$T/refused"; mkmod "$D" good 0 0 0 0; mkmod "$D" bad 0 0 0 0
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'good\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
printf 'bad\tRUN_REFUSED\tunsupported op\t1\t1\t1\t0\t0\t1\t5\t5\t5\t\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules good,bad --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 1 --cycles 4 --present good,bad > "$D/out.txt" 2>&1
check "rc=1 with an exact row set is complete-with-refusals" "$D/census.tsv" good accepted
check "the refused row is refused, not untrusted"            "$D/census.tsv" bad  sim-refused

# --- 2. static_gates=1 but an ACCEPTED sweep row ---------------------------
D="$T/gatefail"; mkmod "$D" g 0 0 0 1
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'g\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules g --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 0 --cycles 4 --present g > "$D/out.txt" 2>&1
check "a failed static gate cannot be ACCEPTED" "$D/census.tsv" g stage-gate-failed

# --- 3. a module handed to the sweep with no row back = infrastructure -----
D="$T/missing"; mkmod "$D" a 0 0 0 0; mkmod "$D" b 0 0 0 0
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules a,b --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 1 --cycles 4 --present a,b > "$D/out.txt" 2>&1; rc=$?
[ "$rc" -eq 2 ] || { echo "FAIL: a missing sweep row must be infrastructure failure (rc=2), got $rc"; fails=$((fails+1)); }
check "a missing sweep row taints the other rows" "$D/census.tsv" a sim-untrusted
[ "$rc" -eq 2 ] && echo "ok: missing sweep row -> rc=2"

# --- 4. duplicate sweep row = infrastructure -------------------------------
D="$T/dup"; mkmod "$D" a 0 0 0 0
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules a --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 1 --cycles 4 --present a > "$D/out.txt" 2>&1; rc=$?
[ "$rc" -eq 2 ] && echo "ok: duplicate sweep row -> rc=2" \
  || { echo "FAIL: a duplicate sweep row must be infrastructure failure, got rc=$rc"; fails=$((fails+1)); }

# --- 5. rc>=2 is infrastructure even with a perfect row set ----------------
D="$T/rc2"; mkmod "$D" a 0 0 0 0
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules a --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 2 --cycles 4 --present a > "$D/out.txt" 2>&1; rc=$?
[ "$rc" -eq 2 ] && echo "ok: sweep rc=2 -> infrastructure failure" \
  || { echo "FAIL: sweep rc>=2 must taint the run, got rc=$rc"; fails=$((fails+1)); }

# --- 6. a short run is not accepted ----------------------------------------
D="$T/short"; mkmod "$D" a 0 0 0 0
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t1\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules a --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 0 --cycles 4 --present a > "$D/out.txt" 2>&1
check "fewer cycles than requested is not accepted" "$D/census.tsv" a sim-incomplete

# --- 7. every requested module gets a row, including blocked ---------------
D="$T/rows"; mkmod "$D" a 0 0 0 0; mkdir -p "$D/mod/blk" "$D/logs"
printf 'BLOCKED\tsome reason\n' > "$D/mod/blk/status"
printf "$TSV_HDR\n" > "$D/sweep.tsv"
printf 'a\tACCEPTED\t\t1\t1\t1\t0\t0\t1\t5\t5\t5\t4\t0.5\t100\t/p\n' >> "$D/sweep.tsv"
python3 "$REP" --out-dir "$D" --modules a,blk,never_ran --sweep-tsv "$D/sweep.tsv" \
  --sweep-rc 0 --cycles 4 --present a > "$D/out.txt" 2>&1
n=$(( $(wc -l < "$D/census.tsv") - 1 ))
[ "$n" -eq 3 ] && echo "ok: all 3 requested modules have a row" \
  || { echo "FAIL: expected 3 rows, got $n -- a module that vanishes looks like a pass"; fails=$((fails+1)); }
check "a blocked module is explicit" "$D/census.tsv" blk blocked

# ---------------------------------------------------------------------------
# REASON EXTRACTION. A blank reason reads as "no diagnostic available" when the
# log in fact had one. The capture was written `([^"]{0,220})"`, which requires
# the closing quote within 220 characters, so every message LONGER than that
# silently produced an empty reason -- and real yosys diagnostics are longer.
# ---------------------------------------------------------------------------
python3 - "$REP" "$T" <<'PYDIAG'
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location("r", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
T = sys.argv[2]; fails = 0

long_msg = "cmd:read_slang " + "x" * 400
p1 = os.path.join(T, "long.log")
open(p1, "w").write('{"severity":"error","code":"yosys-failed","message":"%s"}\n' % long_msg)
got = m.first_diag(p1, "1")
if not got.startswith("cmd:read_slang"):
    print("FAIL: a >220-char diagnostic produced %r instead of the message" % got); fails += 1
else:
    print("ok: a long structured diagnostic is extracted (and truncated)")
if len(got) > 260:
    print("FAIL: the reason was not truncated (%d chars)" % len(got)); fails += 1

p2 = os.path.join(T, "sig.log"); open(p2, "w").write("some noise\n")
for rc, needle in (("143", "NOT a design failure"), ("124", "timed out"),
                   ("134", "SIGABRT"), ("137", "OOM")):
    got = m.first_diag(p2, rc)
    if needle not in got:
        print("FAIL: exit %s -> %r, expected mention of %r" % (rc, got, needle)); fails += 1
    else:
        print("ok: exit %s is explained (%s)" % (rc, needle))

p3 = os.path.join(T, "gate.log"); open(p3, "w").write("== shape ==\nFAIL: no nodes emitted\n")
if "no nodes emitted" not in m.first_diag(p3, "0"):
    print("FAIL: a plain FAIL: line from the static gates was not picked up"); fails += 1
else:
    print("ok: a static-gate FAIL line is picked up")

if m.first_diag(os.path.join(T, "does_not_exist.log"), "0") != "":
    print("FAIL: a missing log should yield an empty reason, not an invention"); fails += 1
else:
    print("ok: a missing log yields no invented reason")
sys.exit(1 if fails else 0)
PYDIAG
[ $? -eq 0 ] || fails=$((fails+1))

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: census_report_test"
