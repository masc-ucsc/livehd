#!/usr/bin/env bash
# Controls for the ONE-ELEMENT unpacked-array port bridge in
# `scripts/gen_cva6_wrappers.py`.
#
# `csr_regfile.sdtrig_decoder_action_o` and `id_stage.sdtrig_decoder_action_i`
# are `logic [63:0] p [0:0]`, which slang types as
#
#     FixedSizeUnpackedArrayType [0:0] of PackedArrayType [63:0] of logic
#
# An unpacked array is not a vector, so the generator used to decline and lose
# both modules -- 2 of the canonical 30.  The bridge carries the packed ELEMENT
# at the gate boundary and wires a one-element internal array to the DUT.
#
# The danger is scope creep: flattening a MULTI-element unpacked array, or one
# whose element width is unknown, would silently change the design.  These
# controls pin BOTH directions of the accepted shape AND the rejections.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
GEN="$ROOT/scripts/gen_cva6_wrappers.py"
WRAPDIR="$ROOT/generated/cva6_wrappers"
ROWS="$HERE/cva6_unpacked_bridge_rows.tsv"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# --- 1. OUTPUT direction, in the generated wrapper --------------------------
w="$WRAPDIR/csr_regfile_gate.sv"
if [[ ! -r "$w" ]]; then
  bad "no csr_regfile wrapper at $w (run scripts/build_cva6_foundation.sh)"
elif ! grep -qx '  output logic \[63:0\] sdtrig_decoder_action_o,' "$w"; then
  bad "csr_regfile: gate port is not the packed element"
elif ! grep -qx '  logic \[63:0\] sdtrig_decoder_action_o_u \[0:0\];' "$w"; then
  bad "csr_regfile: no one-element internal array"
elif ! grep -qx '  assign sdtrig_decoder_action_o = sdtrig_decoder_action_o_u\[0\];' "$w"; then
  bad "csr_regfile: output glue is missing or points the wrong way"
elif grep -q 'assign sdtrig_decoder_action_o_u\[0\] = ' "$w"; then
  bad "csr_regfile: an OUTPUT is being driven into the DUT array"
elif ! grep -q '\.sdtrig_decoder_action_o(sdtrig_decoder_action_o_u)' "$w"; then
  bad "csr_regfile: the DUT is not connected to the array"
else
  ok "output direction: packed element at the boundary, array to the DUT, assign out of [0]"
fi

# --- 2. INPUT direction -----------------------------------------------------
w="$WRAPDIR/id_stage_gate.sv"
if [[ ! -r "$w" ]]; then
  bad "no id_stage wrapper at $w"
elif ! grep -qx '  input  logic \[63:0\] sdtrig_decoder_action_i,' "$w"; then
  bad "id_stage: gate port is not the packed element"
elif ! grep -qx '  logic \[63:0\] sdtrig_decoder_action_i_u \[0:0\];' "$w"; then
  bad "id_stage: no one-element internal array"
elif ! grep -qx '  assign sdtrig_decoder_action_i_u\[0\] = sdtrig_decoder_action_i;' "$w"; then
  bad "id_stage: input glue is missing or points the wrong way"
elif grep -q 'assign sdtrig_decoder_action_i = ' "$w"; then
  bad "id_stage: an INPUT is being driven out of the DUT array"
elif ! grep -q '\.sdtrig_decoder_action_i(sdtrig_decoder_action_i_u)' "$w"; then
  bad "id_stage: the DUT is not connected to the array"
else
  ok "input direction: packed element at the boundary, array to the DUT, assign into [0]"
fi

# --- 3. the bridge must REFUSE everything else ------------------------------
probe="$(python3 - "$GEN" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("g", sys.argv[1])
g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
L64 = {"kind": "PackedArrayType", "range": "[63:0]",
       "elementType": {"kind": "ScalarType"}}
cases = {
  "accept [0:0] of logic[63:0]": ({"kind": "FixedSizeUnpackedArrayType", "range": "[0:0]",
                                   "elementType": L64}, "logic [63:0]"),
  "accept [0:0] of logic":       ({"kind": "FixedSizeUnpackedArrayType", "range": "[0:0]",
                                   "elementType": {"kind": "ScalarType"}}, "logic"),
  "reject [1:0] (two elements)": ({"kind": "FixedSizeUnpackedArrayType", "range": "[1:0]",
                                   "elementType": L64}, None),
  "reject [3:0] (four elements)":({"kind": "FixedSizeUnpackedArrayType", "range": "[3:0]",
                                   "elementType": L64}, None),
  "reject [1:1] (one elem, not index 0)": ({"kind": "FixedSizeUnpackedArrayType", "range": "[1:1]",
                                   "elementType": L64}, None),
  "reject unknown element type": ({"kind": "FixedSizeUnpackedArrayType", "range": "[0:0]",
                                   "elementType": "2593 cva6.some_t"}, None),
  "reject nested unpacked elem": ({"kind": "FixedSizeUnpackedArrayType", "range": "[0:0]",
                                   "elementType": {"kind": "FixedSizeUnpackedArrayType",
                                                   "range": "[0:0]", "elementType": L64}}, None),
  "reject a packed array (not unpacked)": (L64, None),
}
bad = [k for k, (t, want) in cases.items() if g.unpacked_one_elem(t) != want]
print("ALLGOOD" if not bad else "BAD:" + "; ".join(bad))
PY
)"
if [[ "$probe" == "ALLGOOD" ]]; then
  ok "bridge accepts only [0:0] with a computable element width; 6 other shapes refused"
else
  bad "bridge shape policy: $probe"
fi

# --- 4. they actually RUN (not merely generate) -----------------------------
if [[ ! -s "$ROWS" ]]; then
  bad "no recorded rows at $ROWS"
else
  n_ok="$(awk -F'\t' 'NR>1 && $10=="PARSED" && $12=="true" && $13=="RAN"' "$ROWS" | wc -l)"
  n_f="$(awk -F'\t' 'NR>1 && NF!=18' "$ROWS" | wc -l)"
  if [[ "$n_ok" -ne 2 || "$n_f" -ne 0 ]]; then
    bad "recorded rows: $n_ok of 2 PARSED/true/RAN, $n_f wrong-shaped"
  else
    ok "csr_regfile and id_stage: 18-field rows, PARSED / compiles_ok=true / RAN"
  fi
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
