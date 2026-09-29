#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# WINDOWED temporal operators in a formal block — SVA's `##[m:n]`. These are the
# forms that make an implication reach forward in time:
#
#   assert property (@(posedge clk) $rose(req) |-> ##[1:10] $rose(ack));
#   assert(rose(req) implies rose(ack, 1..=10))
#
# Contract under test:
#   * `eventually(x, w)` / `rose(x, w)` / `fell(x, w)` / `changed(x, w)` are
#     EXISTENTIAL over the window (an OR), `always(x, w)` / `stable(x, w)` are
#     UNIVERSAL (an AND) — a true claim PROVES and a claim that is true only for
#     part of the window REFUTES;
#   * the window looks FORWARD from the property's anchor cycle. That is the
#     whole point of the feature and the easiest thing to get backwards, so the
#     directional claims below are asymmetric on purpose: they hold in one
#     direction and refute in the other;
#   * the monitor stays STATELESS. A forward window is implemented by retiming
#     the statement (delay everything by the window's largest offset, so `+k`
#     becomes backward delay `F-k`), NOT by a flop in the monitor — which would
#     be a fresh free symbol per step and silently refute tautologies. So the
#     "property holds STATE" refusal must not fire;
#   * the cycles with no anchor are SKIPPED and the skip is DISCLOSED;
#   * a malformed window is a usage error, never a wrong answer.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_formal_window_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Two-deep delay line: ack is req delayed EXACTLY two cycles (outside reset).
# Exactly-two is what makes the directional claims below decidable — a window
# that includes cycle 2 must hold and one that excludes it must refute.
cat >"$W/dly2.prp" <<'EOF'
pub mod dly2(req:u1) -> (ack:u1@[], nrst:u1@[]) {
  reg r1:u1 = 0
  reg r2:u1 = 0
  reg seen:u1 = 0
  ack  = r2
  r2   = r1
  r1   = req
  seen = 1
  nrst = seen
}
EOF

# `nrst` is the design's own "reset is behind us" flag: `seen` is driven to 1
# unconditionally and resets to 0, so nrst(c)=1 exactly when reset was low at
# c-1. A property can then require a reset-free WINDOW with
# `always(acc.nrst, w)` — itself a use of the universal form under test.
# Guarding with `past(reset)` instead would only cover the anchor cycle, and the
# delay line's output depends on reset being low across the whole window.
#
# ack(c) = req(c-2) provided reset was low at c-1 and c-2. So a claim anchored
# at `a` about ack at a+1 and a+2 needs reset low at a-1, a and a+1, which is
# exactly nrst(a), nrst(a+1), nrst(a+2), i.e. always(acc.nrst, 0..=2).

# ---- existential: the window that CONTAINS the true latency proves ----------
cat >"$W/ev_good.verify.prp" <<'EOF'
const dut = import("dly2.dly2")

formal dly2.ack_follows_req {
  mut acc = dut
  assert((rose(acc.req) and always(acc.nrst, 0..=2))
           implies rose(acc.ack, 1..=3),
         "a req edge is answered within three cycles")
}
EOF
OUT="$W/ev_good.out"
"$LHD" formal verify "$W/dly2.prp" "$W/ev_good.verify.prp" --top dly2 --set formal.bound=8 \
  --workdir "$W/w1" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a true windowed property must pass: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN: $(cat "$OUT")"
# Retiming, not a monitor flop.
! grep -qi 'holds STATE' "$OUT" || fail "a window must not be lowered to monitor state: $(cat "$OUT")"
grep -q 'cycle(s) of history' "$OUT" \
  || fail "the cycles with no anchor must be disclosed: $(cat "$OUT")"

# ---- existential: a window that EXCLUDES the true latency refutes -----------
# The latency is exactly 2, so 3..=4 cannot contain the edge. If the window were
# read BACKWARD (or off by one) this would pass, which is what makes it a
# direction check and not just a negative test.
cat >"$W/ev_bad.verify.prp" <<'EOF'
const dut = import("dly2.dly2")

formal dly2.ack_too_late {
  mut acc = dut
  assert((rose(acc.req) and always(acc.nrst, 0..=4))
           implies rose(acc.ack, 3..=4),
         "FALSE: the answer arrives at cycle 2, not in 3..4")
}
EOF
OUT="$W/ev_bad.out"
"$LHD" formal verify "$W/dly2.prp" "$W/ev_bad.verify.prp" --top dly2 --set formal.bound=8 \
  --workdir "$W/w2" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a window that excludes the real latency must refute: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED: $(cat "$OUT")"

# ---- universal: always() over a window where the signal is NOT always true --
cat >"$W/al_bad.verify.prp" <<'EOF'
const dut = import("dly2.dly2")

formal dly2.ack_not_held {
  mut acc = dut
  assert(rose(acc.req) implies always(acc.ack, 1..=3),
         "FALSE: ack is high for one cycle, not for the whole window")
}
EOF
OUT="$W/al_bad.out"
"$LHD" formal verify "$W/dly2.prp" "$W/al_bad.verify.prp" --top dly2 --set formal.bound=8 \
  --workdir "$W/w3" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "always() over a window it does not hold across must refute: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED for always(): $(cat "$OUT")"

# ---- universal: stable() across a window where the value really is stable ---
# req is a free input, so `ack` is only guaranteed stable when the two cycles
# feeding it are equal; state that as the antecedent and the claim is exact.
cat >"$W/st_good.verify.prp" <<'EOF'
const dut = import("dly2.dly2")

formal dly2.ack_stable_when_req_stable {
  mut acc = dut
  assert((stable(acc.req, 0..=2) and always(acc.nrst, 0..=2))
           implies stable(acc.ack, 2..=2),
         "a held req keeps ack stable two cycles later")
}
EOF
OUT="$W/st_good.out"
"$LHD" formal verify "$W/dly2.prp" "$W/st_good.verify.prp" --top dly2 --set formal.bound=8 \
  --workdir "$W/w4" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a true stable() window must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for stable(): $(cat "$OUT")"

# ---- a window WIDER than the bound must not read as a proof ------------------
# The dangerous failure mode: if the unroll is too short to hold any anchor, the
# obligation is checked nowhere. That must surface as UNKNOWN with the skip
# disclosed, never as a green "proven" over zero cycles — the property below is
# plainly FALSE, so a pass here would be a false proof.
cat >"$W/vac.verify.prp" <<'EOF'
const dut = import("dly2.dly2")

formal dly2.no_anchor {
  mut acc = dut
  assert(always(acc.ack, 1..=30), "FALSE: ack is not always high")
}
EOF
OUT="$W/vac.out"
"$LHD" formal verify "$W/dly2.prp" "$W/vac.verify.prp" --top dly2 --set formal.bound=4 \
  --workdir "$W/w5" --diag-fmt pretty >"$OUT" 2>&1
[ $? -eq 0 ] && fail "a window wider than the bound must not pass: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" && fail "a property checked at NO cycle must not read as proven: $(cat "$OUT")"
grep -q 'cycle(s) of history' "$OUT" \
  || fail "the wholly-unchecked obligation must be disclosed: $(cat "$OUT")"

# ---- malformed windows are usage errors, not answers ------------------------
refuse() {  # BODY  EXPECTED-SUBSTRING  WHAT
  cat >"$W/bad.verify.prp" <<EOF
const dut = import("dly2.dly2")

formal dly2.bad {
  mut acc = dut
  $1
}
EOF
  local out="$W/bad.out"
  "$LHD" formal verify "$W/dly2.prp" "$W/bad.verify.prp" --top dly2 --workdir "$W/wb" \
    --diag-fmt pretty >"$out" 2>&1
  [ $? -ne 0 ] || fail "$3 must be refused: $(cat "$out")"
  grep -qi -- "$2" "$out" || fail "$3: the refusal must explain itself (wanted '$2'): $(cat "$out")"
  rm -rf "$W/wb"
}
refuse 'assert(eventually(acc.ack), "x")'          'bounded window'   'eventually() without a window'
refuse 'assert(always(acc.ack), "x")'              'bounded window'   'always() without a window'
refuse 'assert(rose(acc.ack, 3), "x")'             'must be a window' 'a bare count where a window belongs'
refuse 'assert(acc.ack == past(acc.ack, 1..=2), "x")' 'not a window'  'a window where a count belongs'
refuse 'assert(eventually(acc.ack, 3..=1), "x")'   'empty'            'an inverted window'
refuse 'assert(eventually(acc.ack, 1..=zz), "x")'  'literal cycle'    'a non-literal window bound'
# `1..2` is not Pyrope range syntax at all, so the PARSER refuses it before the
# window code is reached — still an error, just not one this layer authors.

echo "PASS: windowed temporal operators (eventually/always/rose/fell/stable/changed)"
