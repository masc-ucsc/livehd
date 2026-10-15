#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/cgen/tests/memory_state_roundtrip.sv
TOP=memory_state_roundtrip
W="${TEST_TMPDIR:-/tmp/cgen_memory_state_roundtrip_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# The run's verdict: the word on the LAST `lec: '<top>' WORD (...)` line
# (pass/lec/property_types.hpp Outcome). Intermediate lines such as
# "REFUTED under collapse -> flat confirmation" carry no modifier and never
# match; an empty result means no verdict was printed at all.
lec_verdict() {
  grep -aoE "^lec: '[^']*' (PROVEN-full|PROVEN-bounded\([0-9]+\)|TESTED-sampled\([0-9]+\)|TIMEOUT-(time|memory)|REFUTED-(cex|interface|unsupported|unchecked|vacuous|unexplained))( |\$)" "$1" \
    | tail -1 | awk '{print $3}'
}

"$LHD" compile verilog "$SRC" --top "$TOP" \
  --emit-dir pyrope:"$W/gen" --emit-dir lg:"$W/ref_lg" --workdir "$W/w_gen" -q \
  || fail "source Verilog to Pyrope/LGraph"

"$LHD" compile "$W/gen/$TOP.prp" --top "$TOP" \
  --emit-dir verilog:"$W/impl_v" --workdir "$W/w_impl" -q \
  || fail "generated Pyrope to Verilog"

impl="$W/impl_v/$TOP.$TOP.v"
[ -s "$impl" ] || fail "generated Verilog missing: $impl"
grep -q '__lhdmem_h62616e6b5f61_e' "$impl" || fail "bank_a lost cgen memory provenance"
grep -q '__lhdmem_h62616e6b5f62_e' "$impl" || fail "bank_b lost cgen memory provenance"

"$LHD" lec --impl verilog:"$impl" --ref lg:"$W/ref_lg" --top "$TOP" \
  --set formal.effort=proof --set formal.timeout=0 --set lhd.jobs=1 --workdir "$W/w_lec" -q \
  >"$W/lec.log" 2>&1 || { tail -40 "$W/lec.log"; fail "native endpoint LEC"; }
[ "$(lec_verdict "$W/lec.log")" = PROVEN-full ] || { tail -40 "$W/lec.log"; fail "native endpoint did not prove"; }

"$LHD" lec --set formal.effort=proof --set formal.timeout=30 --impl verilog:"$impl" --ref verilog:"$SRC" --top "$TOP" \
  --workdir "$W/w_verilog" -q >"$W/verilog.log" 2>&1 \
  || { tail -40 "$W/verilog.log"; fail "default LEC endpoint LEC or generated-wrapper parse"; }
if grep -qiaE '"verdict"[[:space:]]*:[[:space:]]*"refuted|^lec: [^ ]+ REFUTED-|SETUP FAILED' "$W/verilog.log"; then
  tail -40 "$W/verilog.log"
  fail "default LEC rejected the generated memory wrapper round trip"
fi
case "$(lec_verdict "$W/verilog.log")" in
  PROVEN-full | TIMEOUT-time | TIMEOUT-memory) ;;
  *) tail -40 "$W/verilog.log"; fail "default LEC returned no recognized verdict" ;;
esac

# The whole-array path emits the Memory state inline rather than through a
# cgen_memory_* wrapper. It must carry the same reversible marker: otherwise
# native LEC sees `arr` versus lossy `arr_data` and cannot pair the state.
WHOLE_PRP=inou/cgen/tests/memory_whole_state_roundtrip.prp
WHOLE_SV=inou/cgen/tests/memory_whole_state_roundtrip.sv
WHOLE_TOP=memory_whole_state_roundtrip

"$LHD" compile "$WHOLE_PRP" --top "$WHOLE_TOP" \
  --emit-dir lg:"$W/whole_ref_lg" --emit-dir verilog:"$W/whole_impl_v" \
  --workdir "$W/w_whole_impl" -q \
  || fail "whole-array Pyrope to LGraph/Verilog"

whole_impl="$W/whole_impl_v/$WHOLE_TOP.$WHOLE_TOP.v"
[ -s "$whole_impl" ] || fail "whole-array generated Verilog missing: $whole_impl"
grep -q '__lhdmem_h617272_e_data' "$whole_impl" \
  || fail "inline whole-array state lost cgen memory provenance"

"$LHD" lec --impl verilog:"$whole_impl" --ref lg:"$W/whole_ref_lg" --top "$WHOLE_TOP" \
  --set formal.effort=proof --set formal.timeout=0 --set lhd.jobs=1 --workdir "$W/w_whole_lec" -q \
  >"$W/whole_lec.log" 2>&1 \
  || { tail -40 "$W/whole_lec.log"; fail "whole-array native endpoint LEC"; }
[ "$(lec_verdict "$W/whole_lec.log")" = PROVEN-full ] \
  || { tail -40 "$W/whole_lec.log"; fail "whole-array native endpoint did not prove"; }

"$LHD" lec --set formal.effort=proof --set formal.timeout=30 --impl verilog:"$whole_impl" --ref verilog:"$WHOLE_SV" --top "$WHOLE_TOP" \
  --workdir "$W/w_whole_verilog" -q >"$W/whole_verilog.log" 2>&1 \
  || { tail -40 "$W/whole_verilog.log"; fail "whole-array default LEC endpoint LEC or parse"; }
if grep -qiaE '"verdict"[[:space:]]*:[[:space:]]*"refuted|^lec: [^ ]+ REFUTED-|SETUP FAILED' "$W/whole_verilog.log"; then
  tail -40 "$W/whole_verilog.log"
  fail "default LEC rejected the inline whole-array state round trip"
fi
case "$(lec_verdict "$W/whole_verilog.log")" in
  PROVEN-full | TIMEOUT-time | TIMEOUT-memory) ;;
  *) tail -40 "$W/whole_verilog.log"; fail "whole-array default LEC returned no recognized verdict" ;;
esac

echo "PASS: cgen wrapper and inline memory state provenance survives native and default LEC Verilog round trips"
