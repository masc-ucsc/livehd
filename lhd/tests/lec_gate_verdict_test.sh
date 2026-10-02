#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The CORE-ET LEC gate decision (scripts/lec_gate_verdict.sh), against the three
# verdicts `lhd lec` can return plus the cases that have no verdict at all.
#
# The bug this pins: the gate used to exit only on the literal text REFUTED. An
# UNKNOWN, a solver timeout, a crashed run, and a run that produced NO verdict
# all printed "(LEC gate inconclusive; recorded, not fatal)" and continued --
# and they did that with LEC_STRICT=true too, so strict mode gated nothing while
# reading in the logs as though it had. A certificate generated behind a gate
# that silently passed UNKNOWN carries an equivalence claim nothing established.
#
# The asymmetry that matters: in strict mode a MISSING verdict must FAIL. "No
# counterexample was printed" is not evidence of equivalence.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GATE=""
for c in "${TEST_SRCDIR:-}/_main/scripts/lec_gate_verdict.sh" \
         "${TEST_SRCDIR:-}/scripts/lec_gate_verdict.sh" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/lec_gate_verdict.sh" \
         "scripts/lec_gate_verdict.sh"; do
  [ -r "$c" ] && { GATE="$c"; break; }
done
[ -n "$GATE" ] || { echo "FAIL: cannot find lec_gate_verdict.sh"; exit 1; }

TMP="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/lec_gate_verdict"
rm -rf "$TMP"; mkdir -p "$TMP"
fails=0

mk_json() { printf '{\n  "run": {\n    "verdict": "%s",\n    "detail": ""\n  }\n}\n' "$1" > "$TMP/$2.json"; }
mk_json proven   proven
mk_json refuted  refuted
mk_json unknown  unknown
: > "$TMP/empty.json"
printf 'lec: no verdict was reached; the run died early\n' > "$TMP/noverdict.log"

# A REAL run's shape, copied from generated/core-et/txfma_adder/logs/lec_gate.*
# rather than from the documented taxonomy. The result JSON carries NO verdict
# field; the log's per-block diagnostic says "pass"; the proof is stated by the
# hierarchical summary. The fixtures used to assert the documented shape only,
# so the gate and its test agreed with each other and both were wrong: strict
# mode rejected this exact run.
printf '{"schema_version":1,"tool":"lhd","command":"lec","status":"pass","exit_code":0}\n' \
  > "$TMP/realproof.json"
{
  printf '{"severity":"info","code":"lec-block-proven","pass":"pass.lec","message":"lec block %s pass","verdict":"pass"}\n' "'txfma_adder'"
  printf "lec[hier]: 'txfma_adder' PROVEN (0 child collapses)\n"
  printf 'lec[hier]: 1/1 def(s) proven top-down (0 via cache, 0 via semdiff, 1 via solver)\n'
} > "$TMP/realproof.log"

# ...and the same shape with a definition NOT discharged. A block-level "pass"
# says nothing about the other blocks, so this must not pass strict mode.
{
  printf '{"severity":"info","code":"lec-block-proven","pass":"pass.lec","message":"lec block a pass","verdict":"pass"}\n'
  printf 'lec[hier]: 1/3 def(s) proven top-down (0 via cache, 0 via semdiff, 1 via solver)\n'
} > "$TMP/partial.log"
: > "$TMP/empty.log"

# <desc> <expected-exit> <status> <json> <log> <strict>
check() {
  local desc="$1"; local want="$2"; local st="$3"; local js="$4"; local lg="$5"; local strict="$6"
  local out; out="$("$GATE" "$st" "$js" "$lg" "$strict" 2>&1)"; local got=$?
  if [ "$got" != "$want" ]; then
    echo "FAIL: $desc -- expected exit $want, got $got"; echo "$out" | sed 's/^/      /'; fails=$((fails+1))
  else
    echo "ok: $desc (exit $got)"
  fi
}

echo "--- strict mode: only an explicit proven verdict with exit 0 may pass ---"
check "strict + proven + exit 0 passes"            0  0  "$TMP/proven.json"    "$TMP/empty.log"     true
check "strict + REFUTED is fatal (4)"              4  10 "$TMP/refuted.json"   "$TMP/empty.log"     true
check "strict + UNKNOWN is fatal (5)"              5  1  "$TMP/unknown.json"   "$TMP/empty.log"     true
check "strict + UNKNOWN that exited 0 is fatal"    5  0  "$TMP/unknown.json"   "$TMP/empty.log"     true
check "strict + MISSING verdict is fatal"          5  0  "$TMP/empty.json"     "$TMP/noverdict.log" true
check "strict + no json at all is fatal"           5  0  ""                    ""                   true
check "strict + proven but nonzero exit is fatal"  5  3  "$TMP/proven.json"    "$TMP/empty.log"     true
check "strict + a REAL proof (no verdict field, block says pass, 1/1 defs proven)" \
                                                  0  0  "$TMP/realproof.json" "$TMP/realproof.log" true
check "strict + block pass but only 1/3 defs proven is fatal" \
                                                  5  0  "$TMP/realproof.json" "$TMP/partial.log"   true

echo "--- non-strict: historical behaviour, only a refutation is fatal ---"
check "non-strict + proven passes"                 0  0  "$TMP/proven.json"    "$TMP/empty.log"     false
check "non-strict + UNKNOWN continues"             0  1  "$TMP/unknown.json"   "$TMP/empty.log"     false
check "non-strict + MISSING verdict continues"     0  0  "$TMP/empty.json"     "$TMP/noverdict.log" false
check "non-strict + a REAL proof passes"           0  0  "$TMP/realproof.json" "$TMP/realproof.log" false
check "non-strict + REFUTED is STILL fatal (4)"    4  10 "$TMP/refuted.json"   "$TMP/empty.log"     false

echo "--- a refutation is caught from the log even with no json ---"
printf 'lec: top REFUTED (counterexample at cycle 3)\n' > "$TMP/refuted.log"
check "log-only REFUTED is fatal in strict"        4  10 ""                    "$TMP/refuted.log"   true
check "log-only REFUTED is fatal in non-strict"    4  10 ""                    "$TMP/refuted.log"   false

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: lec_gate_verdict_test"
