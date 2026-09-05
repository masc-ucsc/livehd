#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Standard compile pipeline (pass.cprop + pass.bitwidth) over the yosys-verilog flow:
# bw_mix.v carries one node of each bitwidth-relevant cell class (sum, mult,
# and/or/xor/not, comparators, shl/sra/logic-shift, mux, flop) and must stay
# logically equivalent after bitwidth inference; bw_mem.v drives the memory
# sizing path (no LEC: combinational-memory miters are inconclusive, the
# observable is the generated memory instance).

set -u

LHD=lhd/lhd
MIX=lhd/tests/bw_mix.v
MEM=lhd/tests/bw_mem.v
W="${TEST_TMPDIR:-/tmp/lhd_bw_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. compile of the cell mix + LEC against the source.
"$LHD" compile "$MIX" --reader yosys-verilog --top bw_mix \
  --emit verilog:"$W/bw_mix.gen.v" --workdir "$W/w_mix" -q 2>/dev/null \
  || fail "compile of bw_mix.v failed"
[ -s "$W/bw_mix.gen.v" ] || fail "compile produced empty netlist"
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/bw_mix.gen.v" --ref verilog:"$MIX" --top bw_mix \
  --workdir "$W/w_chk" -q 2>/dev/null \
  || fail "bw_mix optimized netlist is not equivalent to the source"

# 2. The recipe must actually have run pass.bitwidth (including on the default path).
"$LHD" compile "$MIX" --reader yosys-verilog --top bw_mix \
  --emit verilog:"$W/bw_mix2.gen.v" --workdir "$W/w_mix2" --result-json "$W/r.json" -q 2>/dev/null \
  || fail "recompile for recipe check failed"
grep -q 'pass.bitwidth' "$W/r.json" || fail "result recipe does not list pass.bitwidth: $(cat "$W/r.json")"

# 3. Compilation of a synchronous RAM: bitwidth memory sizing must keep the
#    memory instance in the generated Verilog.
"$LHD" compile "$MEM" --reader yosys-verilog --top bw_mem \
  --emit verilog:"$W/bw_mem.gen.v" --workdir "$W/w_mem" -q 2>/dev/null \
  || fail "compile of bw_mem.v failed"
grep -qi 'memory' "$W/bw_mem.gen.v" || fail "optimized memory netlist lost the memory instance"

# 4. A wide right shift observed only through a narrow output must be sized
# at that output, while retaining every input bit the shift can select.
cat > "$W/mux.prp" <<'EOF'
pub comb mux::[timecheck=false](sel:u4, data:u1024) -> (out:u64) {
  out = data >> (sel * 0x40)
}
EOF
cat > "$W/mux_ref.v" <<'EOF'
module mux(input [3:0] sel, input [1023:0] data, output [63:0] out);
  assign out = data[sel * 64 +: 64];
endmodule
EOF
"$LHD" compile "$W/mux.prp" --top mux --emit-dir "lg:$W/mux_lg" \
  --emit "verilog:$W/mux.gen.v" --workdir "$W/w_mux" -q || fail "narrow-output shift compile failed"
"$LHD" tool cat "lg:$W/mux_lg" --diag-fmt jsonl > "$W/mux_graph.jsonl" || fail "mux graph dump failed"
python3 - "$W/mux_graph.jsonl" <<'PY' || fail "right shift did not receive a u64 realization"
import json, sys
rows = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
shifts = {r['nid'] for r in rows if r.get('t') == 'node' and r.get('kind') == 'sra'}
pins = [r for r in rows if r.get('t') == 'pin' and r['nid'] in shifts]
assert len(pins) == 1 and pins[0]['bits'] == 64 and pins[0]['signed'] is False, pins
PY
"$LHD" lec --set formal.solver=lgyosys --impl "verilog:$W/mux.gen.v" \
  --ref "verilog:$W/mux_ref.v" --top mux --workdir "$W/w_mux_lec" -q \
  || fail "narrow-output shift differs from the 16-to-1 word mux"

echo "PASS lhd_bitwidth_test"
