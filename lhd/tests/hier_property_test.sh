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
#      the emitted lgraph carries one property fewer than case 1.
#   3. parent binds a=4        -> VIOLATED, and the build must FAIL.
#
# Case 3 is the point of the test: without it, an implementation that simply
# never checks anything would satisfy cases 1 and 2 and look correct.
#
# The COUNT of obligations left in the emitted lgraph is the evidence: tolg
# materializes each assert/assume as an fproperty Sub node, and an obligation
# that is gone is one that was proven. Only the ones still owed remain — so no
# separate "proven" marker is needed, and an importer re-proving what is present
# is re-proving exactly the outstanding set. Case 0 below checks that removal on
# its own, with no hierarchy involved.
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

# --- 0. a provable assert is discharged, with no hierarchy involved ----------
if $LHD compile "$W/taut.prp" --top taut --emit-dir "lg:$W/TAUT" \
     --workdir "$W/w0" -q >"$W/l0.log" 2>&1; then
  n=$(props "$W/TAUT")
  if [ "$n" -eq 0 ]; then
    echo "ok: a proven assert was removed from the lgraph"
  else
    fail "assert(a<=255) on a u8 is a tautology, but $n obligation(s) remain — a proven obligation must be removed"
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
if $LHD compile "$W/use_ok.prp" --top use_ok --emit-dir "lg:$W/OK" \
     --workdir "$W/w2" -q >"$W/l2.log" 2>&1; then
  n=$(props "$W/OK")
  if [ "$n" -eq 0 ]; then
    echo "ok: parent binding a=3 discharged the assume (0 fproperty left)"
  else
    fail "parent binds a=3 (assume provable) but $n obligation(s) remain — not discharged"
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
