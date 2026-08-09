#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# MEMORY READ FORWARDING under the cvc5 encoder, gated by mutants.
#
# `inou/prp/tests/equiv/mem_fwd_selfwrite` covers the same designs through
# lgcheck's bounded miter. This test drives the SAME pair through `lhd lec`
# (cvc5, the cone/cut decomposition) and -- more importantly -- pairs every
# proof with a MUTANT that must still refute. A forwarding model that silently
# dropped the forward, or ignored the write enable, would make the mutants
# equivalent too, and a proof-only test would call that success.
#
# The pair is a MEMORY (Pyrope) against TWO NAMED FLOPS + muxes (Verilog), with
#   * a self-referential write (`t[wsel] = t[wsel] | a`) -- the write-data cone
#     reads the array's own dout, which ABC sees as a free input; and
#   * a forwarding read at a RUNTIME index -- the dout is a select of the
#     NEXT-state array, not of the committed one (`needs_array` in query.cpp).
#
# It goes through `lg:` deliberately. Routing the Pyrope side out as Verilog and
# back (what `:equiv_engine: cvc5` does) re-reads cgen's memory WRAPPER, whose
# clock arrives through a Sub -- the encoder then refuses it as a derived clock
# and the test would measure that instead of forwarding.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi


W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

cat > "$W/rf.prp" <<'EOF'
pub mod rf(rst:bool, a:u4, wsel:u1, rsel:u1, we:bool) -> (z:u4@[0]) {
  reg t:[2]u4
  if rst {
    t[0] = 0
    t[1] = 0
  } elif we {
    t[wsel] = t[wsel] | a
  }
  z = t[rsel]
}
EOF

# The golden reference: two flops + muxes, forwarding written out by hand.
cat > "$W/ref.v" <<'EOF'
module rf_ref(input clock, input rst, input [3:0] a, input wsel,
              input rsel, input we, output [3:0] z);
  reg [3:0] t0, t1;
  wire [3:0] cur_w = wsel ? t1 : t0;
  wire [3:0] wdata = cur_w | a;
  wire       wr    = we && !rst;
  always @(posedge clock) begin
    if (rst) begin t0<=4'd0; t1<=4'd0; end
    else begin
      if (wr && !wsel) t0<=wdata;
      if (wr &&  wsel) t1<=wdata;
    end
  end
  wire [3:0] cur_r = rsel ? t1 : t0;
  assign z = rst ? 4'd0 : ((wr && (wsel==rsel)) ? wdata : cur_r);
endmodule
EOF

build_prp() { rm -rf "$W/$2"; "$LHD" compile "$1" --top rf --emit-dir "lg:$W/$2" \
                --workdir "$W/w_$2" >"$W/$2.log" 2>&1 \
                || { tail -5 "$W/$2.log"; fail "compile of $1 failed"; }; }
build_v()   { rm -rf "$W/$2"; "$LHD" compile "$1" --reader slang --top rf_ref --emit-dir "lg:$W/$2" \
                --workdir "$W/w_$2" >"$W/$2.log" 2>&1 \
                || { tail -5 "$W/$2.log"; fail "compile of $1 failed"; }; }

lec() {  # <ref_lg> <impl_lg> <tag> -> OUT/RC
  rm -rf "$W/l_$3"
  OUT=$("$LHD" lec --ref "lg:$W/$1" --impl "lg:$W/$2" --ref-top rf_ref --impl-top rf \
        --workdir "$W/l_$3" 2>&1); RC=$?
}

build_prp "$W/rf.prp" impl
build_v   "$W/ref.v"  ref

# ---------------------------------------------------------------------------
# 1. The memory and the flop array agree -- a forwarding read at a runtime
#    index, off an array written from a function of itself.
# ---------------------------------------------------------------------------
lec ref impl ok
if [ "$RC" -ne 0 ]; then
  echo "$OUT" | grep -E "^lec: " | head -1
  fail "case 1: a memory and an equivalent flop-array+mux implementation did not prove (rc=$RC)"
fi
grep -qaiE "refut|not equivalent" <<<"$OUT" && fail "case 1: reported a refutation on an equivalent pair"
echo "ok: a forwarding, self-written memory proves against a flop-array+mux implementation"

# ---------------------------------------------------------------------------
# 2. MUTANT -- drop the forward. The reference reads committed state instead of
#    the same-cycle write. If the encoder does not model forwarding, this looks
#    identical to case 1 and the test above proved nothing.
# ---------------------------------------------------------------------------
sed 's|assign z = rst ? 4.d0 : ((wr \&\& (wsel==rsel)) ? wdata : cur_r);|assign z = rst ? 4'"'"'d0 : cur_r;|' \
  "$W/ref.v" > "$W/ref_nofwd.v"
grep -q "assign z = rst ? 4'd0 : cur_r;" "$W/ref_nofwd.v" || fail "case 2: the mutant sed did not apply"
build_v "$W/ref_nofwd.v" ref_nofwd
lec ref_nofwd impl nofwd
[ "$RC" -eq 0 ] && fail "case 2: a reference WITHOUT forwarding proved equal to a forwarding memory -- the forward is not modelled"
grep -qaiE "refut|not equivalent" <<<"$OUT" \
  || { echo "$OUT" | grep -E "^lec: " | head -1; fail "case 2: expected a REFUTATION for the dropped forward"; }
echo "ok: dropping the forward REFUTES -- the forwarding read is really compared"

# ---------------------------------------------------------------------------
# 3. MUTANT -- forward UNCONDITIONALLY, ignoring the write enable. The classic
#    wrong forwarding model: right whenever we==1, wrong exactly when it is 0.
# ---------------------------------------------------------------------------
sed 's|assign z = rst ? 4.d0 : ((wr \&\& (wsel==rsel)) ? wdata : cur_r);|assign z = rst ? 4'"'"'d0 : ((wsel==rsel) ? wdata : cur_r);|' \
  "$W/ref.v" > "$W/ref_ungated.v"
grep -q "((wsel==rsel) ? wdata : cur_r)" "$W/ref_ungated.v" || fail "case 3: the mutant sed did not apply"
build_v "$W/ref_ungated.v" ref_ungated
lec ref_ungated impl ungated
[ "$RC" -eq 0 ] && fail "case 3: an UNGATED forward proved equal -- the write enable is not part of the forwarding condition"
grep -qaiE "refut|not equivalent" <<<"$OUT" \
  || { echo "$OUT" | grep -E "^lec: " | head -1; fail "case 3: expected a REFUTATION for the ungated forward"; }
echo "ok: forwarding without the write enable REFUTES -- the enable gates the forward"

# ---------------------------------------------------------------------------
# 4. MUTANT -- break the SELF-read in the write data (`t[wsel] | a` -> `a`).
#    This is the half that makes the write-data cone read the array's own dout,
#    which is what ABC cannot see through.
# ---------------------------------------------------------------------------
# `#` as the sed delimiter: the pattern itself contains a `|` (the OR being
# removed), which silently turns `s|...|` into a no-op edit.
sed 's#wire \[3:0\] wdata = cur_w | a;#wire [3:0] wdata = a;#' "$W/ref.v" > "$W/ref_noself.v"
grep -q "wire \[3:0\] wdata = a;" "$W/ref_noself.v" || fail "case 4: the mutant sed did not apply"
build_v "$W/ref_noself.v" ref_noself
lec ref_noself impl noself
[ "$RC" -eq 0 ] && fail "case 4: dropping the self-read from the write data proved equal"
grep -qaiE "refut|not equivalent" <<<"$OUT" \
  || { echo "$OUT" | grep -E "^lec: " | head -1; fail "case 4: expected a REFUTATION for the dropped self-read"; }
echo "ok: dropping the write-data self-read REFUTES -- the read-modify-write is really compared"

# ---------------------------------------------------------------------------
# 5. THE OTHER MEMORY ORDERING. `ordering="old"` is the non-forwarding memory:
#    a same-cycle write is NOT visible to the read. It must pair with a
#    non-forwarding flop array, which is the mirror of case 1 -- so the encoder
#    is modelling the ORDERING and not just always-forward or never-forward.
# ---------------------------------------------------------------------------
sed 's|reg t:\[2\]u4|reg t:[2]u4:[ordering="old"]|' "$W/rf.prp" > "$W/rf_old.prp"
grep -q 'ordering="old"' "$W/rf_old.prp" || fail "case 5: the ordering sed did not apply"
build_prp "$W/rf_old.prp" impl_old

# Same reference as case 2 (no forward) -- that IS the read-old machine.
lec ref_nofwd impl_old old_ok
if [ "$RC" -ne 0 ]; then
  echo "$OUT" | grep -E "^lec: " | head -1
  fail "case 5: an ordering=old memory did not prove against a NON-forwarding flop array (rc=$RC)"
fi
echo 'ok: an ordering="old" memory proves against a non-forwarding flop array'

# ---------------------------------------------------------------------------
# 6. The two orderings are DIFFERENT MACHINES and must not prove equal. Without
#    this, cases 1 and 5 could both pass under a model that ignores `ordering`
#    entirely and happens to match whichever reference it is given.
# ---------------------------------------------------------------------------
lec ref impl_old cross
[ "$RC" -eq 0 ] && fail "case 6: ordering=old proved equal to a FORWARDING reference -- the ordering is not modelled"
grep -qaiE "refut|not equivalent" <<<"$OUT" \
  || { echo "$OUT" | grep -E "^lec: " | head -1; fail "case 6: expected a REFUTATION between the two orderings"; }
echo "ok: the forwarding and read-old orderings are distinguished, not conflated"

echo "PASS: mem_forward_test"
exit 0
