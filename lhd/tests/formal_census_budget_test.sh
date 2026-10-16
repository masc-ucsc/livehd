#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.formal's hierarchy contract preflight must not spend the pass budget on
# a design that has no assumption to discharge.
#
# The preflight solves child `assume`s at their real call-site bindings. Before
# it looks for any, it ran a design-wide occurrence census: a forward-ordered
# walk of every root's virtually-flat hierarchy. That walk scales with the
# number of OCCURRENCES, not definitions -- on minion it was ~9 s of a ~12 s
# pass.formal -- and it ran even when no assume existed anywhere. It ate the
# pass's wall-clock budget (compile.formal.timeout, default 10 s), the budget
# then ran out part-way through the per-module obligations, and which checks
# stayed `runtime_check` changed from one cold compile to the next
# (lhdsuite minion_compile_incremental).
#
#   1. A deep hierarchy (2^13 leaf occurrences, 14 small definitions) with no
#      assume: pass.formal is fast, a 1 s budget is NOT exhausted, and the
#      leaf's tautology assert is proven (its runtime check elided).
#   2. Control: an assume two levels down, bound by the top, is still found and
#      discharged by the preflight (the skip must look through Sub bodies).
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 3; }

W="${TEST_TMPDIR:-/tmp/lhd_formal_census_$$}/census"
rm -rf "$W"
mkdir -p "$W"
rc=0
fail() { echo "FAIL: $*"; rc=1; }

runtime_checks() {  # assert/assume obligations left in an emitted Verilog dir (-1 = nothing emitted)
  local n=0 f found=0
  for f in "$1"/*.v; do
    [ -e "$f" ] || continue
    found=1
    n=$((n + $(grep -cE '^\s*(assert|assume) \(' "$f")))
  done
  [ "$found" -eq 1 ] && echo "$n" || echo -1
}

# --- 1. deep hierarchy, no assume -------------------------------------------
python3 - "$W/deep.prp" <<'PY'
import sys
depth = 13
out = ["pub mod l0(a:U8, b:U8) -> (o:U8@[0]) {", "  assert((a & 1) <= a)", "  mut t = a ^ b"]
out += [f"  t = (t + {i + 1}) ^ (a & {3 * i + 1})" for i in range(40)]
out += ["  wrap o = t", "}"]
for k in range(1, depth + 1):
    out += [f"pub mod l{k}(a:U8, b:U8) -> (o:U8@[0]) {{",
            f"  const x = l{k - 1}(a=a, b=b)",
            f"  const y = l{k - 1}(a=b, b=a)",
            "  o = x ^ y",
            "}"]
open(sys.argv[1], "w").write("\n".join(out) + "\n")
PY
if $LHD compile "$W/deep.prp" --top l13 --emit-dir "verilog:$W/DEEPV" --emit "diagnostics:$W/d1.jsonl" \
     --set compile.formal.timeout=1 --workdir "$W/w1" -q --result-json "$W/r1.json" >"$W/l1.log" 2>&1; then
  if grep -q '"code":"budget-exhausted"' "$W/d1.jsonl" 2>/dev/null; then
    fail "a 1 s formal budget ran out on a design with no assume (the occurrence census is spending it)"
  else
    echo "ok: no assume anywhere -> the 1 s formal budget is not exhausted"
  fi
  a=$(runtime_checks "$W/DEEPV")
  if [ "$a" -eq 0 ]; then
    echo "ok: the leaf tautology is proven and its runtime check elided"
  else
    fail "the leaf tautology assert kept $a runtime check(s) under a 1 s budget"
  fi
  ms=$(python3 -c "import json,sys; print(int(sum(p['ms'] for p in json.load(open(sys.argv[1]))['phases'] if p['name']=='pass.formal')))" "$W/r1.json")
  if [ "$ms" -lt 1000 ]; then
    echo "ok: pass.formal took ${ms} ms over 2^13 leaf occurrences"
  else
    fail "pass.formal took ${ms} ms on 14 small definitions: it scales with occurrences again"
  fi
else
  fail "the deep-hierarchy compile failed:"
  grep -o '"message":"[^"]*"' "$W/l1.log" | head -3 | sed 's/^/      /'
fi

# --- 2. control: an assume two levels down is still discharged ---------------
cat > "$W/gtop.prp" <<'EOF'
pub mod gsub(a:U8) -> (o:U8@[0]) {
  assume(a == 3)
  wrap o = a + 1
}
pub mod gmid(a:U8) -> (o:U8@[0]) { o = gsub(a=a).o }
pub mod gtop(x:U8) -> (o:U8@[0]) { o = gmid(a=3).o }
EOF
if $LHD compile "$W/gtop.prp" --top gtop --emit-dir "verilog:$W/GV" --workdir "$W/w2" -q >"$W/l2.log" 2>&1; then
  a=$(runtime_checks "$W/GV")
  if [ "$a" -eq 0 ]; then
    echo "ok: a grandchild assume bound by the top is discharged (the preflight still runs)"
  else
    fail "the grandchild assume(a==3) bound to 3 by the top kept $a runtime check(s): the preflight was skipped"
  fi
else
  fail "the grandchild-assume compile failed:"
  grep -o '"message":"[^"]*"' "$W/l2.log" | head -3 | sed 's/^/      /'
fi

[ "$rc" -eq 0 ] && echo "PASS: formal_census_budget_test"
exit "$rc"
