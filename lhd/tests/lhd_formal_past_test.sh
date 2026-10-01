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
#
# rose/fell/stable/changed also accept a WINDOW as a second argument
# (`rose(x, 1..=10)`); that is lhd_formal_window_test's subject. Here the only
# claim about the second argument is that a bare COUNT is not one.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_formal_past_$$}"
mkdir -p "$W"

# A REFUTED `formal verify` writes simfail_<test>.prp/.json for the failing
# obligation and then BUILDS AND RUNS that replay -- ~5.5s of host clang each,
# and three of the runs below refute on purpose. The assertions here read the
# REFUTED verdict and the pretty counterexample trace, both of which come from
# the formal engine, so only the replay build is skipped; the witness stays.
fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# One-deep delay line: dout is din delayed a cycle, EXCEPT across reset, where
# the register holds its reset value instead. That exception is what makes the
# unguarded property below genuinely false — the test would be vacuous if the
# design mirrored its input unconditionally.
cat >"$W/dly.prp" <<'EOF'
pub mod dly(din:U8) -> (dout:U8@[]) {
  reg r:U8 = 0
  dout = r
  r = din
}
EOF

# ---- a true past() claim PROVES --------------------------------------------
cat >"$W/good.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.mirrors_past {
  mut acc = dut
  assert((past(acc.`reset`, 1) == 0) implies (acc.dout == past(acc.din, 1)),
         "after reset, dout is din delayed one cycle")
}
EOF
OUT="$W/good.out"
"$LHD" formal verify "$W/dly.prp" "$W/good.verify.prp" --top dly --workdir "$W/wg" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1 \
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
"$LHD" formal verify "$W/dly.prp" "$W/bad.verify.prp" --top dly --workdir "$W/wb" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
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
"$LHD" formal verify "$W/dly.prp" "$W/unguarded.verify.prp" --top dly --workdir "$W/wu" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
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
"$LHD" formal verify "$W/dly.prp" "$W/zero.verify.prp" --top dly --workdir "$W/wz" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1 \
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
"$LHD" formal verify "$W/dly.prp" "$W/expr.verify.prp" --top dly --workdir "$W/we" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
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
"$LHD" formal verify "$W/dly.prp" "$W/nonlit.verify.prp" --top dly --workdir "$W/wn" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a non-literal past() depth must be refused: $(cat "$OUT")"

# ---- rose / fell / stable / changed all reduce to depth-1 history ----------
# On a ONE-BIT line: rose/fell are 1-bit notions, so `changed implies rose or
# fell` holds here and would be FALSE on the u8 line above (5 -> 7 changes
# without either edge).
cat >"$W/dly1.prp" <<'EOF'
pub mod dly1(din:U1) -> (dout:U1@[]) {
  reg r:U1 = 0
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
"$LHD" formal verify "$W/dly1.prp" "$W/edges.verify.prp" --top dly1 --workdir "$W/we2" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1 \
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
"$LHD" formal verify "$W/dly1.prp" "$W/badedge.verify.prp" --top dly1 --workdir "$W/wbe" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "rose == fell must refute: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED for rose == fell: $(cat "$OUT")"

# The same identities on a `Bool` line. The monitor declares a bool port `Bool`
# (bool ports are strict: `acc.dout != 0` would be a type error), so each
# temporal operator must expand to boolean terms -- rose is `(not p1) and p0`,
# not the integer `p1 == 0` form.
cat >"$W/dlyb.prp" <<'EOF'
pub mod dlyb(din:Bool) -> (dout:Bool@[]) {
  reg r:Bool = false
  dout = r
  r = din
}
EOF
cat >"$W/edgesb.verify.prp" <<'EOF'
const dut = import("dlyb.dlyb")

formal dlyb.edges {
  mut acc = dut
  assert(rose(acc.dout)   == ((not past(acc.dout, 1)) and acc.dout), "rose is a false->true step")
  assert(fell(acc.dout)   == (past(acc.dout, 1) and (not acc.dout)), "fell is a true->false step")
  assert(stable(acc.dout) == (acc.dout == past(acc.dout, 1)),        "stable is no change")
  assert(changed(acc.dout) implies (rose(acc.dout) or fell(acc.dout)), "a change is a rise or a fall")
  assert(always(acc.dout, 0..=1) implies eventually(acc.dout, 0..=1), "always implies eventually")
}
EOF
OUT="$W/edgesb.out"
"$LHD" formal verify "$W/dlyb.prp" "$W/edgesb.verify.prp" --top dlyb --workdir "$W/web" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "the temporal identities over a bool port must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the bool edge identities: $(cat "$OUT")"

# History inside a SUBMODULE-bound block, over a `reg t:Bool` of the instance.
# The monitor declares the register `Bool` like a bool port (so `not acc.t` and
# `acc.t == acc.q` type-check), and a history port must read an EARLIER cycle
# in every instance context: the per-instance binds once dropped the delay, so
# `past(acc.t, 1)` read the current value and the false claim below PROVED.
cat >"$W/tgl.prp" <<'EOF'
mod leaf(en:Bool) -> (q:Bool@[0]) {
  reg t:Bool = false
  if en {
    t = not t
  }
  q = t
}
pub mod tgl(en:Bool) -> (q:Bool@[0]) {
  const inst0 = leaf(en = en)
  q = inst0
}
EOF
cat >"$W/tgl.verify.prp" <<'EOF'
const sub = import("tgl.leaf")

formal leaf.hist {
  mut acc = sub
  assert(acc.t == acc.q, "the register drives q")
  assert(rose(acc.t) implies past(acc.en, 1), "t rises only after an enabled cycle")
}
EOF
OUT="$W/tgl.out"
"$LHD" formal verify "$W/tgl.prp" "$W/tgl.verify.prp" --top tgl --workdir "$W/wt" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "history over an instance's bool register must prove: $(cat "$OUT")"
grep -q '__p_t:Bool' "$W/wt/__fbmon_0.prp" || fail "a bool register must be a bool monitor port: $(cat "$W/wt/__fbmon_0.prp")"
cat >"$W/tglbad.verify.prp" <<'EOF'
const sub = import("tgl.leaf")

formal leaf.frozen {
  mut acc = sub
  assert(acc.t == past(acc.t, 1), "WRONG: t never changes")
}
EOF
OUT="$W/tglbad.out"
"$LHD" formal verify "$W/tgl.prp" "$W/tglbad.verify.prp" --top tgl --workdir "$W/wtb" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "past() in an instance context must read an earlier cycle: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED for a toggling register claimed frozen: $(cat "$OUT")"

# ---- arity is enforced ------------------------------------------------------
cat >"$W/arity.verify.prp" <<'EOF'
const dut = import("dly.dly")

formal dly.bad_arity {
  mut acc = dut
  assert(rose(acc.dout, 2) == 0, "a bare count is not a window")
}
EOF
OUT="$W/arity.out"
"$LHD" formal verify "$W/dly.prp" "$W/arity.verify.prp" --top dly --workdir "$W/wa" \
  --set formal.simfail_run=false --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "rose() with a bare count must be refused: $(cat "$OUT")"
grep -qi 'must be a window' "$OUT" || fail "the refusal must point at the window syntax: $(cat "$OUT")"

echo "PASS: lhd formal verify past/rose/fell/stable/changed"
