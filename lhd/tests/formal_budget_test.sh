#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.formal's TOTAL wall-clock budget (`--set compile.formal.timeout`, seconds,
# default 10, 0 = unbounded). Property checking is otherwise unbounded compile
# time -- on minion it was 52.5 s of a 55 s compile -- so the pass stops asking
# cvc5 once the budget is gone.
#
# The contract has exactly two halves, and BOTH have to be checked or a trivial
# implementation passes:
#
#   1. It BINDS. With a budget too small to solve anything, the obligations that
#      the default budget discharges are NOT discharged: their runtime checks
#      survive into the netlist, and the pass says so once (budget-exhausted).
#   2. It DEGRADES SOUNDLY, never fails. Running out of budget is not an error:
#      the compile still exits 0 and still emits a netlist. An unsolved property
#      is a KEPT runtime check, which is what the design would have had anyway.
#
# Case 0 is the baseline the other two are read against: with the default budget
# these same tautologies are proven and their checks elided. Without it, "the
# checks survived" would prove nothing -- they might have survived anyway.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 3; }

W="${TEST_TMPDIR:-/tmp/lhd_formal_budget_$$}"
mkdir -p "$W"
rc=0
fail() { echo "FAIL: $*"; rc=1; }

# Many obligations, each a tautology the prover discharges in milliseconds. The
# COUNT is the load-bearing part: a sub-millisecond budget cannot survive the
# first solve, so every obligation after it is skipped outright -- which is what
# makes case 2 deterministic instead of a race with one lucky query.
{
  echo 'pub mod budget(a:u8, b:u8) -> (o:u8@[0]) {'
  for i in $(seq 0 31); do
    echo "  assert((a & $i) <= a)"
  done
  echo '  o = a + b'
  echo '}'
} > "$W/budget.prp"

# Runtime checks left in an emitted Verilog directory. cgen spells an assert
# obligation `assert (` inside `synthesis translate_off`. An empty directory is a
# failed compile, not a clean netlist: report -1 so it cannot read as "elided".
runtime_checks() {
  local n=0 f found=0
  for f in "$1"/*.v; do
    [ -e "$f" ] || continue
    found=1
    n=$((n + $(grep -cE '^\s*assert \(' "$f")))
  done
  [ "$found" -eq 1 ] && echo "$n" || echo -1
}

# --- 0. baseline: the default budget proves them all -------------------------
if $LHD compile "$W/budget.prp" --top budget --emit-dir "verilog:$W/BASEV" \
     --emit "diagnostics:$W/d0.jsonl" --workdir "$W/w0" -q >"$W/l0.log" 2>&1; then
  a=$(runtime_checks "$W/BASEV")
  if [ "$a" -eq 0 ]; then
    echo "ok: under the default budget every tautology is proven and its runtime check elided"
  else
    fail "the default budget should discharge all 32 tautologies, but $a runtime check(s) survived"
  fi
  if grep -q 'budget-exhausted' "$W/d0.jsonl" 2>/dev/null; then
    fail "the default 10s budget was reported exhausted on a design of 32 tautologies"
  fi
else
  fail "the baseline compile failed:"
  grep -o '"message":"[^"]*"' "$W/l0.log" | head -3 | sed 's/^/      /'
fi

# --- 1. an unbounded budget (0) is still the old behavior --------------------
if $LHD compile "$W/budget.prp" --top budget --emit-dir "verilog:$W/ZEROV" \
     --set compile.formal.timeout=0 --workdir "$W/w1" -q >"$W/l1.log" 2>&1; then
  a=$(runtime_checks "$W/ZEROV")
  if [ "$a" -eq 0 ]; then
    echo "ok: timeout=0 (unbounded) proves them all, exactly as the default did"
  else
    fail "timeout=0 means UNBOUNDED, not zero budget, but $a runtime check(s) survived"
  fi
else
  fail "compile with timeout=0 failed"
fi

# --- 2. a budget too small to solve binds, and only defers -------------------
# Sound degrade: exit 0, a netlist, and the obligations kept as runtime checks.
if $LHD compile "$W/budget.prp" --top budget --emit-dir "verilog:$W/TINYV" \
     --emit "diagnostics:$W/d2.jsonl" \
     --set compile.formal.timeout=0.001 --workdir "$W/w2" -q >"$W/l2.log" 2>&1; then
  a=$(runtime_checks "$W/TINYV")
  if [ "$a" -gt 0 ]; then
    echo "ok: a 1 ms budget stops the proofs; $a runtime check(s) kept in the netlist"
  elif [ "$a" -eq 0 ]; then
    fail "a 1 ms budget still discharged all 32 obligations; the budget is not bound to the solver"
  else
    fail "no netlist was emitted under a tiny budget; running out of budget must not break the compile"
  fi
  # The declared diagnostics channel, not stdout: `-q` keeps warnings off the
  # terminal, and a warning nobody can read is the same as no warning.
  if grep -q '"code":"budget-exhausted"' "$W/d2.jsonl" 2>/dev/null; then
    echo "ok: the pass reported the exhausted budget"
  else
    fail "the exhausted budget was never reported; a silent drop of every proof is the worst outcome"
  fi
else
  fail "an exhausted formal budget must NOT fail the build (it only defers to runtime checks):"
  grep -o '"message":"[^"]*"' "$W/l2.log" | head -3 | sed 's/^/      /'
fi

# --- 3. a malformed budget is a usage error, not a silent default ------------
if $LHD compile "$W/budget.prp" --top budget --set compile.formal.timeout=soon \
     --workdir "$W/w3" -q >"$W/l3.log" 2>&1; then
  fail "compile.formal.timeout=soon was accepted; a typo must not silently buy the default budget"
else
  echo "ok: a non-numeric budget is rejected"
fi

[ "$rc" -eq 0 ] && echo "PASS: pass.formal's wall-clock budget binds, degrades soundly, and is reported"
exit "$rc"
