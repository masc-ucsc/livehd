#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `past(x, n)` in a formal block: the FIRST temporal operator. A property may
# name a signal's value n cycles earlier, which a purely combinational monitor
# cannot express. Contract under test:
#   * `past(x, n)` resolves to the value x held n cycles ago — a true claim
#     PROVES and a false one REFUTES with a per-cycle counterexample;
#   * the monitor stays STATELESS: history is resolved by the ENGINE indexing
#     the unroll (Monitor::Bind::delay), never by a flop inside the monitor,
#     which would be a fresh free symbol per step and silently refute
#     tautologies. `past` must therefore NOT trip the "property holds STATE"
#     refusal;
#   * a cycle with less than n cycles of history behind it cannot witness the
#     property, so those obligations are SKIPPED and the skip is DISCLOSED in
#     the verdict (never silently vacuous);
#   * two different depths compose in one property;
#   * `past(x, 0)` is x, and a non-literal depth / non-signal argument is a
#     usage error rather than a wrong answer.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_formal_past_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# One-deep delay line: dout is din delayed a cycle, EXCEPT across reset, where
# the register holds its reset value instead. That exception is what makes the
# unguarded property below genuinely false — the test would be vacuous if the
# design mirrored its input unconditionally.
cat >"$W/dly.prp" <<'EOF'
pub mod dly(din:u8) -> (dout:u8@[]) {
  reg r:u8 = 0
  dout = r
  r = din
}
EOF

# ---- a true past() claim PROVES --------------------------------------------
cat >"$W/good.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.mirrors_past {
  mut acc = dut
  assert((past(acc.reset, 1) == 0) implies (acc.dout == past(acc.din, 1)),
         "after reset, dout is din delayed one cycle")
}
EOF
OUT="$W/good.out"
"$LHD" formal verify "$W/dly.prp" "$W/good.verify.prp" --top dly --workdir "$W/wg" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a true past() property must pass: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN: $(cat "$OUT")"
# The stateless-monitor refusal must NOT fire: past() is engine-resolved history,
# not a flop in the monitor.
! grep -qi 'holds STATE' "$OUT" || fail "past() must not be treated as monitor state: $(cat "$OUT")"

# ---- the skipped-history window is DISCLOSED -------------------------------
grep -q 'cycle(s) of history' "$OUT" \
  || fail "the cycles skipped for want of history must be disclosed: $(cat "$OUT")"

# ---- a false past() claim REFUTES with a trace ------------------------------
cat >"$W/bad.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.wrong_depth {
  mut acc = dut
  assert(acc.dout == past(acc.din, 2), "WRONG: claims a two-cycle delay")
}
EOF
OUT="$W/bad.out"
"$LHD" formal verify "$W/dly.prp" "$W/bad.verify.prp" --top dly --workdir "$W/wb" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a false past() property must fail the run: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED: $(cat "$OUT")"
grep -q 'counterexample inputs' "$OUT" || fail "a refuted past() must carry the input trace: $(cat "$OUT")"

# ---- an UNGUARDED mirror claim is false across reset ------------------------
# Not a tautology check: the register is forced to its reset value while reset
# is asserted, so `dout == past(din,1)` is violated there. Catching that is what
# proves the history sample tracks the DESIGN's cycles rather than being wired
# to the current-cycle value (which would make this vacuously true).
cat >"$W/unguarded.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.unguarded {
  mut acc = dut
  assert(acc.dout == past(acc.din, 1), "false across reset")
}
EOF
OUT="$W/unguarded.out"
"$LHD" formal verify "$W/dly.prp" "$W/unguarded.verify.prp" --top dly --workdir "$W/wu" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "an unguarded mirror claim must refute across reset: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED for the unguarded claim: $(cat "$OUT")"

# ---- past(x, 0) is x --------------------------------------------------------
cat >"$W/zero.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.depth_zero {
  mut acc = dut
  assert(past(acc.dout, 0) == acc.dout, "past(x, 0) is x")
}
EOF
OUT="$W/zero.out"
"$LHD" formal verify "$W/dly.prp" "$W/zero.verify.prp" --top dly --workdir "$W/wz" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "past(x, 0) must be the current value: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for past(x,0): $(cat "$OUT")"

# ---- misuse is a usage error, not a wrong answer ----------------------------
cat >"$W/expr.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.expr_arg {
  mut acc = dut
  assert(past(acc.din + 1, 1) == 0, "an expression argument is not supported yet")
}
EOF
OUT="$W/expr.out"
"$LHD" formal verify "$W/dly.prp" "$W/expr.verify.prp" --top dly --workdir "$W/we" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "past() over an expression must be refused: $(cat "$OUT")"
grep -qi 'past' "$OUT" || fail "the refusal must name past(): $(cat "$OUT")"

cat >"$W/nonlit.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.nonliteral_depth {
  mut acc = dut
  assert(acc.dout == past(acc.din, acc.din), "a non-literal depth is not a cycle count")
}
EOF
OUT="$W/nonlit.out"
"$LHD" formal verify "$W/dly.prp" "$W/nonlit.verify.prp" --top dly --workdir "$W/wn" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a non-literal past() depth must be refused: $(cat "$OUT")"

# ---- rose / fell / stable / changed all reduce to depth-1 history ----------
# On a ONE-BIT line: rose/fell are 1-bit notions, so `changed implies rose or
# fell` holds here and would be FALSE on the u8 line above (5 -> 7 changes
# without either edge).
cat >"$W/dly1.prp" <<'EOF'
pub mod dly1(din:u1) -> (dout:u1@[]) {
  reg r:u1 = 0
  dout = r
  r = din
}
EOF
# Proven as IDENTITIES against hand-written past() forms, so a wrong expansion
# (swapped edge sense, off-by-one depth) refutes instead of quietly passing.
cat >"$W/edges.verify.prp" <<'EOF'
const dut = import("dly1.dly1")

formal dly1.edges {
  mut acc = dut
  assert(rose(acc.dout)    == ((past(acc.dout, 1) == 0) and (acc.dout != 0)), "rose is a 0->1 step")
  assert(fell(acc.dout)    == ((past(acc.dout, 1) != 0) and (acc.dout == 0)), "fell is a 1->0 step")
  assert(stable(acc.dout)  == (acc.dout == past(acc.dout, 1)),                "stable is no change")
  assert(changed(acc.dout) == (not stable(acc.dout)),                         "changed is the negation of stable")
  assert(changed(acc.dout) implies (rose(acc.dout) or fell(acc.dout)),        "a change is a rise or a fall")
}
EOF
OUT="$W/edges.out"
"$LHD" formal verify "$W/dly1.prp" "$W/edges.verify.prp" --top dly1 --workdir "$W/we2" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "rose/fell/stable/changed identities must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the edge identities: $(cat "$OUT")"
! grep -qi 'holds STATE' "$OUT" || fail "edge operators must not be monitor state: $(cat "$OUT")"

# a WRONG edge sense must refute — the identities above are only meaningful if
# the expansion is actually checked
cat >"$W/badedge.verify.prp" <<'EOF'
const dut = import("dly1.dly1")

formal dly1.bad_edge {
  mut acc = dut
  assert(rose(acc.dout) == fell(acc.dout), "WRONG: a rise is not a fall")
}
EOF
OUT="$W/badedge.out"
"$LHD" formal verify "$W/dly1.prp" "$W/badedge.verify.prp" --top dly1 --workdir "$W/wbe" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "rose == fell must refute: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED for rose == fell: $(cat "$OUT")"

# ---- arity is enforced ------------------------------------------------------
cat >"$W/arity.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.bad_arity {
  mut acc = dut
  assert(rose(acc.dout, 2) == 0, "rose takes one argument")
}
EOF
OUT="$W/arity.out"
"$LHD" formal verify "$W/dly.prp" "$W/arity.verify.prp" --top dly --workdir "$W/wa" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "rose() with two arguments must be refused: $(cat "$OUT")"

echo "PASS: lhd formal verify past/rose/fell/stable/changed"
