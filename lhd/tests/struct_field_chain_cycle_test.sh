#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The per-field WRITE CHAIN false combinational loop on a NESTED packed-struct
# VARIABLE, and the pass/cprop fix that dissolves it.
#
# A nested packed-struct variable cannot be split into per-leaf nets (that path
# is one level deep), so it lowers to ONE packed bus: `c.f0 = ..; c.f1 = ..;
# c.f2 = ..` inside a single always_comb becomes a Set_mask CHAIN on that net.
# A later read of one field then binds to the LAST version of the whole word,
# so if any field written after it depends (however indirectly) on that read,
# the WORD-level graph closes a loop the bit-level design does not have. The
# fields never alias, the per-field dataflow is a DAG, and Verilator
# --lint-only -Wall reports no UNOPTFLAT on either fixture.
#
# The fix is in pass/cprop scalar_get_mask_packed's Set_mask arm, and it has
# TWO legs which this test pins SEPARATELY (each fixture needs exactly one, and
# both fixtures fail with neither):
#
#   DISJOINT  a Set_mask whose lane is provably disjoint from the slice being
#             read cannot affect it -> walk past it to the base `a`.
#             (struct_field_chain_disjoint: the read skips the `.imm` write.)
#
#   CONTAINED a slice lying entirely inside the lane reads back exactly what
#             `value` put there -> re-base onto `value`, dropping the
#             dependency on `a` and hence on the whole earlier write chain.
#             (struct_field_chain_own_lane: the read lands ON the `.rm` write,
#             so there is nothing to skip; without this leg that Set_mask's `a`
#             operand keeps the `.typ` write -- and the cycle -- upstream.)
#
# WHY THIS TEST EXISTS SEPARATELY FROM THE EQUIV PAIRS. The fixtures live in
# inou/prp/tests/equiv (they are real corpus entries and are reused here as
# data, so there is one source of truth), but NONE of the harnesses over that
# corpus can gate this bug:
#   * prp-equiv-*  lowers the .prp and never reads the golden Verilog at all;
#   * prp-v2prp2v-* proves through yosys lgcheck, which packs the struct
#                    happily; its native cvc5 leg DOES refuse but deliberately
#                    scores that refusal as INCONCLUSIVE, not a failure. That
#                    gate is correct and must not be tightened for this bug.
# So the strict assertion lives here: the round trip must come back PROVEN, with
# no cycle refusal anywhere.

set -u

LHD="${LHD:-lhd/lhd}"
EQUIV="${EQUIV_DIR:-inou/prp/tests/equiv}"
W="${TEST_TMPDIR:-/tmp/lhd_struct_field_chain_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# verilog -> lgraph (ref) and verilog -> pyrope -> lgraph (impl), then LEC the
# two. This is the shape that refuses when the false cycle survives: the encoder
# reports `no encodable driver (combinational cycle?)` with a WORD-LEVEL CYCLE
# chain and nothing is compared.
roundtrip_must_prove() {  # <name> <top>
  local name=$1 top=$2 v="$EQUIV/$1.v"
  [ -f "$v" ] || fail "$name: golden not found at $v"

  rm -rf "$W/$name"
  mkdir -p "$W/$name"

  "$LHD" compile "$v" --reader slang --top "$top" \
        --emit-dir "lg:$W/$name/ref" --workdir "$W/$name/wr" >"$W/$name/compile.log" 2>&1 \
    || fail "$name: verilog compile failed: $(cat "$W/$name/compile.log")"

  "$LHD" compile "$v" --reader slang --top "$top" \
        --emit-dir "pyrope:$W/$name/prp" --workdir "$W/$name/wp" >>"$W/$name/compile.log" 2>&1 \
    || fail "$name: pyrope emission failed: $(cat "$W/$name/compile.log")"

  "$LHD" compile "$W/$name/prp/$top.prp" --top "$top" \
        --emit-dir "lg:$W/$name/impl" --workdir "$W/$name/wi" >>"$W/$name/compile.log" 2>&1 \
    || fail "$name: re-compile of the emitted pyrope failed: $(cat "$W/$name/compile.log")"

  "$LHD" lec --impl "lg:$W/$name/impl" --ref "lg:$W/$name/ref" --top "$top" \
        --workdir "$W/$name/wl" >"$W/$name/lec.log" 2>&1
  local rc=$?

  # The bug's signature: the encoder cannot order the word-level SCC.
  ! grep -qa "WORD-LEVEL CYCLE" "$W/$name/lec.log" \
    || fail "$name: word-level combinational cycle survived:
$(grep -ao 'WORD-LEVEL CYCLE through: [^]"]*' "$W/$name/lec.log" | head -1)"

  [ "$rc" -eq 0 ] || fail "$name: lec exited $rc: $(tail -3 "$W/$name/lec.log")"

  # Exit 0 is necessary but not sufficient -- `lhd lec` also exits 0 on an
  # INCONCLUSIVE. Demand the positive verdict so this can never pass vacuously.
  grep -qa "PROVEN equivalent" "$W/$name/lec.log" \
    || fail "$name: lec exited 0 without proving equivalence: $(tail -3 "$W/$name/lec.log")"

  echo "ok: $name round-tripped and PROVEN equivalent"
}

# Leg 1: the read skips a DISJOINT later field write.
roundtrip_must_prove struct_field_chain_disjoint struct_field_chain_disjoint_top

# Leg 2: the read lands on the write of its OWN lane and must re-base onto
# `value`; the disjoint skip alone cannot dissolve this one.
roundtrip_must_prove struct_field_chain_own_lane struct_field_chain_own_lane_top

echo "PASS: nested struct per-field write chains do not form a word-level cycle"
