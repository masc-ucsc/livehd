#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass/lec memory <-> storage-flop bank bridge (pass/lec/query.cpp
# find_mem_entry_bank + the BMC / inductive twins): a resetless 1rd/1wr register
# file (lhd/tests/lec_membank.sv, 8 x 8) compiled from Verilog, bit-blasted by
# pass.abc memory=true (the default) and mapped through gensim cell models, must
# PROVE against the compiled design with cvc5 -- UNBOUNDED, the inductive
# flop-cut miter pairs every storage cell with its array entry -- in the three
# shapes the mapped netlist can take:
#   1. one-bit DFF cells `mem__mem<i>_<b>` on a Q cell (test.lib DFFx1);
#   2. the same cells on a QN-only cell (test_qn.lib DFFNx1, `Flop(Not(D))`
#      model): the model's state must BE the pin, or the tie shares the
#      complement and the unwritten read refutes on ASAP7's DFFHQNx1 only;
#   3. whole bits-wide native flops `mem__mem<i>` (register_max_bits=1 keeps the
#      region's registers native, so mem_lower's storage flops survive as
#      `always @(posedge)` registers).
# Before the bridge every one of these refuted at checked step 1 on a read of a
# never-written entry (rd_data ref=all-ones impl=all-ones-but-one: two free
# power-on symbols). The negative control is the SAME bank shape with a
# corrupted write address (BAD=1): the tie applies and the write path must
# still REFUTE, so the bridge cannot hide a wrong netlist. lgyosys cross-checks
# shape 1 (must not refute).
#
# Hermetic: the small vendored Liberties (inou/prp/tests/abc/test.lib,
# test_qn.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
QLIB=inou/prp/tests/abc/test_qn.lib
SRC=lhd/tests/lec_membank.sv
W="${TEST_TMPDIR:-/tmp/lhd_lec_membank_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$QLIB" ] || fail "missing liberty $QLIB"
[ -f "$SRC" ] || fail "missing fixture $SRC"

# compile_design <dir> <slang -G...>: slang -> lg:<dir>/lg (the LEC reference).
compile_design() {
  local d="$1" gparam="$2"
  mkdir -p "$d"
  "$LHD" compile "$SRC" --reader slang --top membank --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1" \
      -q --result-json "$d/r.json" -- "$gparam" || fail "compile $gparam -> $(cat "$d/r.json" 2>/dev/null)"
}
# map_design <dir> <lib> [extra pass.abc --set ...]: color + pass.abc (memory=true
# default) -> lg:<dir>/net, plus the gensim models of <lib> -> lg:<dir>/models.
map_design() {
  local d="$1" lib="$2"
  shift 2
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  run pass color synth --top membank.membank lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top membank.membank lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$lib" \
      --emit diagnostics:"$d/diag.jsonl" --workdir "$d/w3" "$@"
  ! grep -q '"code":"memory-unlowered"' "$d/diag.jsonl" || fail "$d: memory was NOT bit-blasted: $(grep memory-unlowered "$d/diag.jsonl")"
  run pass liberty gensim "$lib" --emit-dir lg:"$d/models" --workdir "$d/w5"
  run compile lg:"$d/net" --top membank.membank --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
}
# lec_cvc5 <netlist dir> <ref dir> <out json>: netlist as IMPL (the direction
# mem_lower's refinements are sound in), compiled design as REF, cell models.
lec_cvc5() {
  local nd="$1" rd="$2" out="$3"
  "$LHD" lec --impl lg:"$nd/net" --ref lg:"$rd/lg" --lib lg:"$nd/models" --top membank.membank \
      --set formal.solver=cvc5 --workdir "$nd/wc5" -q --result-json "$out" > "$out.log" 2>&1
  echo $?
}
verdict() { grep -o '"lec":{[^}]*}' "$1"; }

GOOD="$W/good"
compile_design "$GOOD" -GBAD=0

# ---------------------------------------------------------------------------
# 1. one-bit DFF cells on a Q cell: PROVEN, unbounded
# ---------------------------------------------------------------------------
D="$W/q"
cp -r "$GOOD/lg" "$D/lg" 2>/dev/null || { mkdir -p "$D" && cp -r "$GOOD/lg" "$D/lg"; }
map_design "$D" "$LIB"
[ "$(grep -c "^\s*DFFx1 " "$D/netv/membank.v")" -eq 64 ] || fail "q: expected 64 DFFx1 storage cells (8 x 8), got $(grep -c '^\s*DFFx1 ' "$D/netv/membank.v")"
grep -q "mem__mem3_5" "$D/netv/membank.v" || fail "q: storage cells are not named mem__mem<i>_<b>: $(grep -m3 DFFx1 "$D/netv/membank.v")"
rc=$(lec_cvc5 "$D" "$GOOD" "$D/lec.json")
[ "$rc" -eq 0 ] || fail "q: cvc5 lec exited $rc: $(cat "$D/lec.json" 2>/dev/null)"
grep -q '"verdict":"proven"' "$D/lec.json" || fail "q: cvc5 did not PROVE the bit-blasted register file: $(verdict "$D/lec.json")"
grep -q '"bounded":false' "$D/lec.json" || fail "q: proof is only BOUNDED (the inductive bank twin did not pair the cells): $(verdict "$D/lec.json")"
echo "PASS: 8x8 resetless register file as 64 DFFx1 cells is PROVEN (unbounded) against its Memory"

# lgyosys on the Verilog (the lhdtrack lec_netlist cross-check): must not refute
run() { "$LHD" "$@" -q --result-json "$D/r.json" || fail "$* -> $(cat "$D/r.json" 2>/dev/null)"; }
run compile lg:"$D/models" --recipe O0 --emit-dir verilog:"$D/modelsv" --workdir "$D/w7"
run compile lg:"$GOOD/lg" --top membank.membank --recipe O0 --emit-dir verilog:"$D/refv" --workdir "$D/w8"
cat "$D/netv/"*.v "$D/modelsv/"*.v > "$D/impl.v"
cat "$D/refv/"*.v > "$D/ref.v"
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$D/impl.v" --ref verilog:"$D/ref.v" --top membank \
    --workdir "$D/wy" -q --result-json "$D/lec_yosys.json" > "$D/lec_yosys.log" 2>&1 \
  || fail "q: lgyosys lec failed: $(cat "$D/lec_yosys.json" 2>/dev/null)"
! grep -q '"verdict":"refuted"' "$D/lec_yosys.json" || fail "q: lgyosys REFUTED the bit-blasted register file"
echo "PASS: lgyosys does not refute the mapped register file"

# ---------------------------------------------------------------------------
# 2. the same cells on a QN-only cell: the Flop(Not(D)) model keeps state == pin
# ---------------------------------------------------------------------------
D="$W/qn"
mkdir -p "$D" && cp -r "$GOOD/lg" "$D/lg"
map_design "$D" "$QLIB"
[ "$(grep -c "^\s*DFFNx1 " "$D/netv/membank.v")" -eq 64 ] || fail "qn: expected 64 DFFNx1 storage cells, got $(grep -c '^\s*DFFNx1 ' "$D/netv/membank.v")"
rc=$(lec_cvc5 "$D" "$GOOD" "$D/lec.json")
[ "$rc" -eq 0 ] || fail "qn: cvc5 lec exited $rc: $(cat "$D/lec.json" 2>/dev/null)"
grep -q '"verdict":"proven"' "$D/lec.json" || fail "qn: cvc5 did not PROVE the QN-cell register file (model state != pin?): $(verdict "$D/lec.json")"
grep -q '"bounded":false' "$D/lec.json" || fail "qn: proof is only BOUNDED: $(verdict "$D/lec.json")"
echo "PASS: the same register file on QN-only DFFNx1 cells is PROVEN (unbounded) through the Flop(Not(D)) model"

# ---------------------------------------------------------------------------
# 3. whole native storage flops mem__mem<i> (register_max_bits=1)
# ---------------------------------------------------------------------------
D="$W/native"
mkdir -p "$D" && cp -r "$GOOD/lg" "$D/lg"
map_design "$D" "$LIB" --set pass.abc.register_max_bits=1
[ "$(grep -c "^\s*DFFx1 " "$D/netv/membank.v")" -eq 0 ] || fail "native: register_max_bits=1 still mapped DFF cells"
grep -q "mem__mem3\b\|mem__mem3 " "$D/netv/membank.v" || fail "native: no whole storage register mem__mem<i> in the netlist: $(grep -m3 posedge "$D/netv/membank.v")"
rc=$(lec_cvc5 "$D" "$GOOD" "$D/lec.json")
[ "$rc" -eq 0 ] || fail "native: cvc5 lec exited $rc: $(cat "$D/lec.json" 2>/dev/null)"
grep -q '"verdict":"proven"' "$D/lec.json" || fail "native: cvc5 did not PROVE the native storage flops: $(verdict "$D/lec.json")"
grep -q '"bounded":false' "$D/lec.json" || fail "native: proof is only BOUNDED: $(verdict "$D/lec.json")"
echo "PASS: whole native storage flops mem__mem<i> are PROVEN (unbounded) against the Memory"

# ---------------------------------------------------------------------------
# 4. negative control: a corrupted write address in the SAME bank shape refutes
# ---------------------------------------------------------------------------
BAD="$W/bad"
compile_design "$BAD" -GBAD=1
map_design "$BAD" "$LIB"
[ "$(grep -c "^\s*DFFx1 " "$BAD/netv/membank.v")" -eq 64 ] || fail "bad: expected 64 DFFx1 storage cells, got $(grep -c '^\s*DFFx1 ' "$BAD/netv/membank.v")"
rc=$(lec_cvc5 "$BAD" "$GOOD" "$BAD/lec.json")
[ "$rc" -ne 0 ] || fail "bad: cvc5 lec exited 0 on a netlist that writes the wrong entry: $(verdict "$BAD/lec.json")"
grep -q '"verdict":"refuted"' "$BAD/lec.json" || fail "bad: expected REFUTED for the corrupted write address, got $(verdict "$BAD/lec.json")"
grep -q '"class":"equiv_fail"' "$BAD/lec.json" || fail "bad: refutation is not an equiv_fail: $(cat "$BAD/lec.json")"
echo "PASS: a netlist with a corrupted write address in the same bank shape is REFUTED (the tie hides nothing)"

echo "PASS: pass/lec memory <-> storage-flop bank bridge (Q cells, QN cells, native flops, negative control)"
