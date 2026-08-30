#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Formal obligations across a MODULE BOUNDARY: a submodule states `assume(a==3)`
# on one of its inputs, and whether that obligation is discharged depends on what
# the PARENT binds to `a`. The contract:
#
#   1. sub compiled ALONE      -> the obligation is KEPT in the lgraph. `a` is a
#      free input here, so the assume is neither provable nor violated; it is the
#      importer that still owes the proof. Compiling a submodule on its own must
#      not fail on it (its inputs are constrained by a parent that is not present).
#   2. parent binds a=3        -> now PROVABLE, so the obligation is DISCHARGED:
#      the fproperty node stays (marked `proven`) and its runtime check is gone
#      from the emitted netlist.
#   3. parent binds a=4        -> VIOLATED, and the build must FAIL.
#
# Case 3 is the point of the test: without it, an implementation that simply
# never checks anything would satisfy cases 1 and 2 and look correct.
#
# tolg materializes each assert/assume as an fproperty Sub node, and pass.formal
# never deletes one: a discharged obligation is MARKED `proven` (the channel
# cgen elides the runtime check off, and verify/LEC re-adjudicate off), so the
# evidence is two-fold -- the node count in the emitted lgraph (still present)
# and the runtime-check count in the emitted Verilog (gone). Case 0 below
# checks that marking on its own, with no hierarchy involved.
#
# The top-rooted contract walk implements this without a second modular verify
# driver: selected-top IO assumptions remain active/unchecked, while a parent
# occurrence proves or refutes each child input obligation at its actual bind.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: lhd binary not found"; exit 3; }

W="${TEST_TMPDIR:-/tmp/lhd_hier_prop_$$}"
mkdir -p "$W"
rc=0
fail() { echo "FAIL: $*"; rc=1; }

cat > "$W/taut.prp" <<'EOF'
pub mod taut(a:u8) -> (o:u8@[0]) {
  assert(a <= 255)
  o = a
}
EOF
cat > "$W/lib_sub.prp" <<'EOF'
pub mod sub(a:u8) -> (o:u8@[0]) {
  assume(a == 3)
  o = a + 1
}
EOF
cat > "$W/use_ok.prp" <<'EOF'
const lib_sub = import("lib_sub")
pub mod use_ok(x:u8) -> (o:u8@[0]) { o = lib_sub.sub(a=3).o }
EOF
cat > "$W/use_bad.prp" <<'EOF'
const lib_sub = import("lib_sub")
pub mod use_bad(x:u8) -> (o:u8@[0]) { o = lib_sub.sub(a=4).o }
EOF
cat > "$W/explicit_nocheck.prp" <<'EOF'
pub mod explicit_nocheck(a:u8) -> (o:u8@[0]) {
  assume_nocheck(a < 4)
  o = a
}
EOF

# Obligations still owed in an emitted library == fproperty Sub instances. NOT
# the `fproperty` line in library.txt: that is the module DECLARATION, one per
# library no matter how many (or few) instances survive.
props() {
  $LHD tool cat "lg:$1" --workdir "$1.catwd" 2>/dev/null | grep -c '"kind":"sub","name":"ass'
}

# Runtime checks left in an emitted Verilog directory. cgen spells an assert
# obligation `assert (` and an assume obligation `assume (` (both inside
# `synthesis translate_off`), so BOTH must be counted or a surviving assume is
# invisible to the test. The `lgassert` range-select guard is emitted for a
# bit-range select regardless of any property, so it is deliberately NOT
# matched. An empty directory (no .v emitted) is a failure of the compile, not
# a clean netlist: report -1 so the caller cannot mistake it for "elided".
runtime_checks() {
  local n=0 f found=0
  for f in "$1"/*.v; do
    [ -e "$f" ] || continue
    found=1
    n=$((n + $(grep -cE '^\s*(assert|assume) \(' "$f")))
  done
  [ "$found" -eq 1 ] && echo "$n" || echo -1
}

# --- 0. a provable assert is discharged, with no hierarchy involved ----------
# DISCHARGED means MARKED, not deleted. pass.formal records the result on the
# node as `proven`; it does not edit the graph. That attribute is the channel
# consumers read — cgen elides the runtime check off it, and `lhd formal
# verify` / `lhd lec` re-adjudicate off it — so deleting the node would throw
# away the very marker the proof produced.
#
# The observable contract is therefore: the node SURVIVES, and its runtime check
# is GONE from the emitted netlist.
if $LHD compile "$W/taut.prp" --top taut --emit-dir "lg:$W/TAUT" --emit-dir "verilog:$W/TAUTV" \
     --workdir "$W/w0" -q >"$W/l0.log" 2>&1; then
  n=$(props "$W/TAUT")
  a=$(runtime_checks "$W/TAUTV")
  if [ "$n" -ge 1 ] && [ "$a" -eq 0 ]; then
    echo "ok: a proven assert is marked (node kept) and its runtime check elided"
  elif [ "$n" -lt 1 ]; then
    fail "the fproperty node was removed; a discharged obligation must be MARKED proven, not deleted"
  else
    fail "assert(a<=255) on a u8 is a tautology, but its runtime check survived into the netlist ($a)"
  fi
else
  fail "the tautology assert should compile cleanly"
fi

# --- 1. the submodule on its own keeps the obligation ------------------------
if $LHD compile "$W/lib_sub.prp" --top sub --emit-dir "lg:$W/SOLO" \
     --workdir "$W/w1" -q >"$W/l1.log" 2>&1; then
  n=$(props "$W/SOLO")
  if [ "$n" -ge 1 ]; then
    echo "ok: sub alone keeps its obligation ($n fproperty)"
  else
    fail "sub alone dropped the obligation (fproperty count $n); the importer can no longer discharge it"
  fi
else
  fail "sub alone did not compile — a submodule's input assume must not fail its own build:"
  grep -o '"message":"[^"]*"' "$W/l1.log" | head -1 | sed 's/^/      /'
fi

# --- 2. a parent that satisfies the assume discharges it ---------------------
if $LHD compile "$W/use_ok.prp" --top use_ok --emit-dir "lg:$W/OK" --emit-dir "verilog:$W/OKV" \
     --workdir "$W/w2" -q >"$W/l2.log" 2>&1; then
  n=$(props "$W/OK")
  a=$(runtime_checks "$W/OKV")
  # Same rule as case 0: discharged == marked proven, so the node stays and only
  # the runtime obligation disappears.
  if [ "$n" -ge 1 ] && [ "$a" -eq 0 ]; then
    echo "ok: parent binding a=3 discharged the assume (marked proven, no runtime check)"
  elif [ "$n" -lt 1 ]; then
    fail "the discharged assume was deleted; it must be marked proven and kept"
  else
    fail "parent binds a=3 (assume provable) but a runtime check survived ($a)"
  fi
else
  fail "parent binding a=3 should compile cleanly"
fi

# --- 3. a parent that violates the assume must FAIL --------------------------
if $LHD compile "$W/use_bad.prp" --top use_bad --emit-dir "lg:$W/BAD" \
     --workdir "$W/w3" -q >"$W/l3.log" 2>&1; then
  fail "parent binds a=4, violating assume(a==3), but the build PASSED"
else
  echo "ok: parent binding a=4 was refuted"
fi

# --- 4. disabling checks keeps every assume active --------------------------
if $LHD compile "$W/use_bad.prp" --top use_bad --set formal.assume_check=false \
     --emit-dir "lg:$W/NOCHECK_ALL" --workdir "$W/w4" >"$W/l4.log" 2>&1; then
  n=$(props "$W/NOCHECK_ALL")
  if [ "$n" -ge 1 ]; then
    echo "ok: formal.assume_check=false kept the violated child assume active ($n fproperty)"
  else
    fail "formal.assume_check=false dropped the assumption instead of retaining it"
  fi
else
  fail "formal.assume_check=false must disable the check without removing the assume"
fi
grep -q 'formal-unchecked-assume' "$W/l4.log" \
  || fail "formal.assume_check=false must warn that the assume is active and unchecked"

# --- 5. design-body assume_nocheck is a first-class spelling ----------------
if $LHD compile "$W/explicit_nocheck.prp" --top explicit_nocheck \
     --emit-dir "lg:$W/EXPLICIT_NOCHECK" --workdir "$W/w5" >"$W/l5.log" 2>&1; then
  n=$(props "$W/EXPLICIT_NOCHECK")
  [ "$n" -ge 1 ] || fail "assume_nocheck compiled but its active fproperty was dropped"
  grep -q 'formal-unchecked-assume' "$W/l5.log" \
    || fail "assume_nocheck must be disclosed as active and unchecked"
  echo "ok: design-body assume_nocheck remains active"
else
  fail "design-body assume_nocheck must compile"
fi

exit $rc
