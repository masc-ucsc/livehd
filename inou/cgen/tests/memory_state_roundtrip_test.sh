#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/cgen/tests/memory_state_roundtrip.sv
TOP=memory_state_roundtrip
W="${TEST_TMPDIR:-/tmp/cgen_memory_state_roundtrip_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile verilog "$SRC" --top "$TOP" --recipe O1 \
  --emit-dir pyrope:"$W/gen" --emit-dir lg:"$W/ref_lg" --workdir "$W/w_gen" -q \
  || fail "source Verilog to Pyrope/LGraph"

"$LHD" compile "$W/gen/$TOP.prp" --top "$TOP" --recipe O0 \
  --emit-dir verilog:"$W/impl_v" --workdir "$W/w_impl" -q \
  || fail "generated Pyrope to Verilog"

impl="$W/impl_v/$TOP.$TOP.v"
[ -s "$impl" ] || fail "generated Verilog missing: $impl"
grep -q '__lhdmem_h62616e6b5f61_e' "$impl" || fail "bank_a lost cgen memory provenance"
grep -q '__lhdmem_h62616e6b5f62_e' "$impl" || fail "bank_b lost cgen memory provenance"

"$LHD" lec --impl verilog:"$impl" --ref lg:"$W/ref_lg" --top "$TOP" \
  --set formal.timeout=0 --set formal.jobs=1 --workdir "$W/w_lec" -q \
  >"$W/lec.log" 2>&1 || { tail -40 "$W/lec.log"; fail "native endpoint LEC"; }
grep -q 'PROVEN equivalent' "$W/lec.log" || { tail -40 "$W/lec.log"; fail "native endpoint did not prove"; }

LGCHECK_EQUIV_TIMEOUT=30 "$LHD" lec --impl verilog:"$impl" --ref verilog:"$SRC" --top "$TOP" \
  --set formal.solver=lgyosys --set formal.lec.gold_reader=slang --set formal.lec.gate_reader=slang \
  --workdir "$W/w_lgyosys" -q >"$W/lgyosys.log" 2>&1 \
  || { tail -40 "$W/lgyosys.log"; fail "lgyosys endpoint LEC or generated-wrapper parse"; }
if grep -qia 'REFUTED\|not equivalent\|SETUP FAILED' "$W/lgyosys.log"; then
  tail -40 "$W/lgyosys.log"
  fail "lgyosys rejected the generated memory wrapper round trip"
fi
grep -Eq 'PROVEN equivalent|INCONCLUSIVE' "$W/lgyosys.log" \
  || { tail -40 "$W/lgyosys.log"; fail "lgyosys returned no recognized verdict"; }

# The whole-array path emits the Memory state inline rather than through a
# cgen_memory_* wrapper. It must carry the same reversible marker: otherwise
# native LEC sees `arr` versus lossy `arr_data` and cannot pair the state.
WHOLE_PRP=inou/cgen/tests/memory_whole_state_roundtrip.prp
WHOLE_SV=inou/cgen/tests/memory_whole_state_roundtrip.sv
WHOLE_TOP=memory_whole_state_roundtrip

"$LHD" compile "$WHOLE_PRP" --top "$WHOLE_TOP" --recipe O0 \
  --emit-dir lg:"$W/whole_ref_lg" --emit-dir verilog:"$W/whole_impl_v" \
  --workdir "$W/w_whole_impl" -q \
  || fail "whole-array Pyrope to LGraph/Verilog"

whole_impl="$W/whole_impl_v/$WHOLE_TOP.$WHOLE_TOP.v"
[ -s "$whole_impl" ] || fail "whole-array generated Verilog missing: $whole_impl"
grep -q '__lhdmem_h617272_e_data' "$whole_impl" \
  || fail "inline whole-array state lost cgen memory provenance"

"$LHD" lec --impl verilog:"$whole_impl" --ref lg:"$W/whole_ref_lg" --top "$WHOLE_TOP" \
  --set formal.timeout=0 --set formal.jobs=1 --workdir "$W/w_whole_lec" -q \
  >"$W/whole_lec.log" 2>&1 \
  || { tail -40 "$W/whole_lec.log"; fail "whole-array native endpoint LEC"; }
grep -q 'PROVEN equivalent' "$W/whole_lec.log" \
  || { tail -40 "$W/whole_lec.log"; fail "whole-array native endpoint did not prove"; }

LGCHECK_EQUIV_TIMEOUT=30 "$LHD" lec --impl verilog:"$whole_impl" --ref verilog:"$WHOLE_SV" --top "$WHOLE_TOP" \
  --set formal.solver=lgyosys --set formal.lec.gold_reader=slang --set formal.lec.gate_reader=slang \
  --workdir "$W/w_whole_lgyosys" -q >"$W/whole_lgyosys.log" 2>&1 \
  || { tail -40 "$W/whole_lgyosys.log"; fail "whole-array lgyosys endpoint LEC or parse"; }
if grep -qia 'REFUTED\|not equivalent\|SETUP FAILED' "$W/whole_lgyosys.log"; then
  tail -40 "$W/whole_lgyosys.log"
  fail "lgyosys rejected the inline whole-array state round trip"
fi
grep -Eq 'PROVEN equivalent|INCONCLUSIVE' "$W/whole_lgyosys.log" \
  || { tail -40 "$W/whole_lgyosys.log"; fail "whole-array lgyosys returned no recognized verdict"; }

echo "PASS: cgen wrapper and inline memory state provenance survives native and lgyosys Verilog round trips"
