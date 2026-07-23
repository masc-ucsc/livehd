#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for `lhd lec --trust <def>` / formal.lec.trust: a def ASSUMED equal
# WITHOUT a proof — the escape hatch for a cell the encoder cannot model yet (a
# latch). The bottom-up driver SKIPS proving the trusted def and force-blackboxes
# its instances, so the latch-free rest still proves; the trust is DISCLOSED
# ("PROVEN under N trusted def(s)"), a real divergence OUTSIDE the trusted cone
# still REFUTES (trust hides only the leaf, never out-of-leaf logic), and trusting
# the top itself is refused (that would assume the whole design — a vacuous pass).

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lectrust}"; mkdir -p "$WORK"; fail=0

# A real LATCH leaf: Pyrope `reg :[latch=true]` is the level-sensitive latch the
# M2 encoder does not model ("sequential op 'latch' not supported yet"). Each top
# variant gets its OWN directory (with a copy of leaf) so sibling-import discovery
# never sees two `mod top` definitions.
leaf() {  # $1 = dir
  cat > "$1/leaf.prp" <<'EOF'
pub mod leaf(g:u1, d:u8) -> (q:u8@[]) {
  reg ql:u8:[latch=true]
  if g == 1 {
    ql = d
  }
  q = ql
}
EOF
}
mkdir -p "$WORK/base" "$WORK/rdiff"
leaf "$WORK/base"; leaf "$WORK/rdiff"
# base: o = leaf(d=p); r = p ^ s   (r is combinational, OUTSIDE the leaf)
cat > "$WORK/base/top.prp" <<'EOF'
const leaf = import("leaf.leaf")
pub mod top(g:u1, p:u8, s:u8) -> (o:u8@[], r:u8@[]) {
  mut u = leaf::[name=u](g = g, d = p)
  o = u
  r = p ^ s
}
EOF
# rdiff: a REAL divergence OUTSIDE the leaf (r = p & s instead of p ^ s)
cat > "$WORK/rdiff/top.prp" <<'EOF'
const leaf = import("leaf.leaf")
pub mod top(g:u1, p:u8, s:u8) -> (o:u8@[], r:u8@[]) {
  mut u = leaf::[name=u](g = g, d = p)
  o = u
  r = p & s
}
EOF

C() { "$LHD" compile "$1/top.prp" --top top --emit-dir "lg:$2" --workdir "$3" >/dev/null 2>&1; }
C "$WORK/base"  "$WORK/lg_base"  "$WORK/c1"
C "$WORK/rdiff" "$WORK/lg_rdiff" "$WORK/c2"

run() {  # $1=label ; $2..=lhd lec args ; sets RC/OUT (default hier driver)
  OUT=$("$LHD" lec "${@:2}" --top top --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) NO trust: the latch leaf is scheduled for proof and the encoder REFUSES it.
#    semdiff=none forces the solver — else the identical ref/impl are dropped as
#    structurally-equal with NO encoding, so the latch is never reached.
run refuse --impl "lg:$WORK/lg_base" --ref "lg:$WORK/lg_base" --set formal.lec.semdiff=none
if [ "$RC" -eq 0 ]; then echo "FAIL: untrusted latch leaf not refused (rc=0)"; fail=1
elif ! echo "$OUT" | grep -qi "latch"; then echo "FAIL: refusal is not about a latch"; echo "$OUT" | tail -3; fail=1
else echo "ok: untrusted latch leaf -> encoder REFUSES"; fi

# 2) --trust leaf: the latch is assumed equal (skipped + boxed) -> top PROVEN,
#    and the assumption is DISCLOSED (never a silent unconditional pass).
run trust --impl "lg:$WORK/lg_base" --ref "lg:$WORK/lg_base" --trust leaf
if [ "$RC" -ne 0 ]; then echo "FAIL: --trust leaf rc=$RC (want PROVEN)"; echo "$OUT" | tail -4; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: --trust leaf: top not PROVEN"; fail=1
elif ! echo "$OUT" | grep -qi "trusted"; then echo "FAIL: --trust leaf: assumption not disclosed"; fail=1
else echo "ok: --trust leaf -> PROVEN under a disclosed trusted def"; fi

# 3) soundness: a REAL divergence OUTSIDE the leaf (r=p^s vs p&s) still REFUTES
#    with --trust leaf — trust boxes only the leaf, never out-of-leaf logic.
run sound --impl "lg:$WORK/lg_base" --ref "lg:$WORK/lg_rdiff" --trust leaf
if [ "$RC" -eq 0 ]; then echo "FAIL: real out-of-leaf diff rc=0 (want REFUTED)"; echo "$OUT" | tail -3; fail=1
elif ! echo "$OUT" | grep -qi "refut\|not equivalent"; then echo "FAIL: out-of-leaf diff not REFUTED"; echo "$OUT" | tail -3; fail=1
else echo "ok: a real diff outside a trusted leaf -> REFUTED (bug still caught)"; fi

# 4) trusting the TOP would assume the whole design equal (a vacuous pass): the
#    CLI refuses it with a usage error, not a clean exit.
run toperr --impl "lg:$WORK/lg_base" --ref "lg:$WORK/lg_base" --trust top
if [ "$RC" -eq 0 ]; then echo "FAIL: --trust top accepted (vacuous pass)"; fail=1
elif ! echo "$OUT" | grep -qi "top module\|vacuous\|whole design"; then
  echo "FAIL: --trust top: wrong error message"; echo "$OUT" | tail -3; fail=1
else echo "ok: --trust <top> refused (vacuous-pass guard)"; fi

if [ "$fail" -eq 0 ]; then echo "PASS: lec_trust_test"; else echo "FAIL: lec_trust_test"; fi
exit "$fail"
