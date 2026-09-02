#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc memory bit-blast (`pass.abc.memory=true`, the default; pass/abc/
# mem_lower.cpp) on the shapes that were wrong or refused before the
# constant-address / per-lane rework:
#
#   1. A constant-index MULTI-WRITER register tile (lhd/tests/abc_memtile.sv: N
#      entries x 8 bits, one always_ff per entry = one CONSTANT-address write
#      port per entry (+ one for its synchronous reset), two RUNTIME-address read
#      ports). The bedrock br_fifo_shared_dynamic_flops data/pointer tiles and
#      br_flow_deserializer are this shape. The old fold built, per (entry, port),
#      an all-constant `EQ` plus a per-bit getbit/and2/mux/concat chain: 97 ABC
#      input nodes per 32-bit pair, and the EQ was miscompiled to a parity
#      compare so each write selected every same-parity entry (REFUTED by both
#      solvers, report_9681). Now a constant-address port touches ITS entry only,
#      through one bits-wide Mux per lane.
#        - N=32 (256 storage bits, the reported tile): maps through the hermetic
#          Liberty; no memory instance survives; 256 DFF cells; and two guards
#          pinned at 2x their measured value so the fold cannot silently grow
#          back: ABC input nodes <= 2 per storage bit (measured 359 = 1.4/bit;
#          the old fold handed ABC 26,434 = 103/bit for the resetless tile) and
#          mapped comb cells <= 12 per storage bit (measured 2,674 = 10.4/bit:
#          ~5 for the reset+enable write muxes, ~5 for the two 32:1 read
#          ports; in an ASAP7 lib that write path is ONE O2A1O1I/AO22 per bit,
#          the report's "<= 1 mux-class cell per storage bit" -- the hermetic
#          2-input lib spells a 2:1 mux as 3 NAND2 + 1 INV).
#        - N=8: the same shape LEC'd against the compiled source with gensim
#          cell models, with BOTH solvers: lgyosys (`lhd lec` must pass -- the
#          yosys flow's memory-bearing verdict is the bounded miter, exit 0, the
#          same standard //lhd/tests:lhd_abc_seq_test and mem_ordering_test
#          apply to abc_mem / w) and cvc5 (verdict must be `proven`; bounded
#          from reset today -- the Memory's power-on array and the per-entry
#          flops are unpaired state until the pass/lec init bridge lands, which
#          is also why the source carries a synchronous reset: without it cvc5
#          refutes at step 1 on an unwritten entry read, a false REFUTE). N=8
#          because the cvc5 array encoding of the 2N-port reference is
#          superlinear in ports (N=8: 2 s; N=16: >120 s UNKNOWN) and the yosys
#          bounded miter on N=32 needs >40 minutes.
#   2. read_all (lhd/tests/abc_memall.sv): a packed array read WHOLE
#      (`assign all = mem`, the Memory cell's reserved read_all driver) -- the
#      br_tracker_linked_list_ctrl `ll_head` shape, which mem_lower used to
#      refuse ("unmodeled memory output"). Must lower (no memory-unlowered
#      diagnostic, no native array in the netlist) and LEC with both solvers,
#      which pins the bit layout (entry 0 in the low bits).
#   3. memory=false must still keep the memory a native cgen_memory instance,
#      and memory_max_bits must keep an over-limit memory native with the
#      one-line `memory-max-bits` note (and 0 must disable the guard).
#   4. The all-constant EQ width bug the tile exposed, in isolation:
#      `x[3:0] == 8'd100` (and `== 8'd20`, whose low 5 bits are 4) must map to
#      constant 0 -- it used to map to NOR4(x0,x1,!x2,x3) = (x == 4) because the
#      compare width came from the 4-bit operand alone.
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
TILE=lhd/tests/abc_memtile.sv
MEMALL=lhd/tests/abc_memall.sv
W="${TEST_TMPDIR:-/tmp/lhd_abc_memlower_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$TILE" ] || fail "missing fixture $TILE"
[ -f "$MEMALL" ] || fail "missing fixture $MEMALL"

metric() {  # metric <name> <json>: first "<name>":<number> in the file (the qor `total` block comes first)
  grep -o "\"$1\":[0-9]*" "$2" | head -1 | cut -d: -f2
}
cells() {  # cells <netlist.v>: standard-cell instance lines (one per instantiated Liberty cell)
  grep -c "^\s*\(NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1\) " "$1"
}

# map_design <dir> <src> <top> <slang -G...> [extra pass.abc --set ...]: compile
# through slang, color, tech-map with the DEFAULT memory mode, emit the netlist
# Verilog. Leaves abc.json (result + qor) and diag.jsonl in <dir>.
map_design() {
  local d="$1" src="$2" top="$3" gparam="$4"
  shift 4
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  # `-- -G<param>=<value>` hands the override to slang; everything after `--`
  # is slang's, so lhd's own -q/--result-json must come BEFORE it.
  local gargs=()
  [ -z "$gparam" ] || gargs=(-- "$gparam")
  "$LHD" compile "$src" --reader slang --top "$top" --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1" \
      -q --result-json "$r" "${gargs[@]}" || fail "compile $src $gparam -> $(cat "$r" 2>/dev/null)"
  run pass color synth --top "$top.$top" lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top "$top.$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$LIB" \
      --emit diagnostics:"$d/diag.jsonl" --workdir "$d/w3" "$@"
  cp "$r" "$d/abc.json"
  run compile lg:"$d/net" --top "$top.$top" --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
}

# lec_both <dir> <top> [cvc5 bound]: the original-logic twin (pass partition) +
# gensim cell models, then the mapped netlist must be equivalent under BOTH
# solvers. The cvc5 bound defaults to the pass's own (6); a caller lowers it
# when the whole-array compare makes the deeper unrolling slow.
lec_both() {
  local d="$1" top="$2" bound="${3:-}"
  local bset=()
  [ -z "$bound" ] || bset=(--set "formal.bound=$bound")
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  run pass partition --top "$top.$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w4"
  run pass liberty gensim "$LIB" --emit-dir lg:"$d/models" --workdir "$d/w5"
  run compile lg:"$d/models" --recipe O0 --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  run compile lg:"$d/re" --top "$top.$top" --recipe O0 --emit-dir verilog:"$d/rev" --workdir "$d/w8"
  cat "$d/netv/"*.v "$d/modelsv/"*.v > "$d/impl.v"
  cat "$d/rev/"*.v > "$d/ref.v"
  # cvc5: graph-level, netlist as IMPL (the direction mem_lower's refinements are sound in)
  "$LHD" lec --impl lg:"$d/net" --ref lg:"$d/re" --lib lg:"$d/models" --top "$top.$top" \
      --set formal.solver=cvc5 "${bset[@]}" --workdir "$d/wc5" -q --result-json "$d/lec_cvc5.json" \
    || fail "$top: cvc5 lec failed: $(cat "$d/lec_cvc5.json" 2>/dev/null)"
  grep -q '"verdict":"proven"' "$d/lec_cvc5.json" \
    || fail "$top: cvc5 lec did not PROVE the bit-blasted memory: $(grep -o '"lec":{[^}]*}' "$d/lec_cvc5.json")"
  # lgyosys: Verilog-level (the lhdtrack lec_netlist path)
  "$LHD" lec --set formal.solver=lgyosys --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" \
      --workdir "$d/wc" -q --result-json "$d/lec_yosys.json" \
    || fail "$top: lgyosys lec failed: $(cat "$d/lec_yosys.json" 2>/dev/null)"
  ! grep -q '"verdict":"refuted"' "$d/lec_yosys.json" || fail "$top: lgyosys REFUTED the bit-blasted memory"
}

# ---------------------------------------------------------------------------
# 1a. N=32 tile: lowered, guards on ABC input nodes and mapped cells
# ---------------------------------------------------------------------------
D="$W/tile32"
map_design "$D" "$TILE" memtile -GN=32
! grep -q '"code":"memory-unlowered"' "$D/diag.jsonl" || fail "tile32: memory was NOT bit-blasted: $(grep memory-unlowered "$D/diag.jsonl")"
! grep -hq "cgen_memory\|_data\b" "$D/netv/"*.v || fail "tile32: a native memory instance survived memory=true"
dff=$(grep -c "^\s*DFFx1 " "$D/netv/memtile.v")
[ "$dff" -eq 256 ] || fail "tile32: expected 256 storage DFF cells (32 x 8), got $dff"
bits=256
nodes=$(metric input_nodes "$D/abc.json")
gates=$(metric gates "$D/abc.json")
[ -n "$nodes" ] && [ -n "$gates" ] || fail "tile32: no qor in $(cat "$D/abc.json")"
[ "$nodes" -le $((2 * bits)) ] \
  || fail "tile32: $nodes ABC input nodes for $bits storage bits (> 2/bit): the per-(entry,port) fold grew back"
[ "$gates" -le $((12 * bits)) ] \
  || fail "tile32: $gates mapped cells for $bits storage bits (> 12/bit): write path is no longer one mux per lane"
ncells=$(cells "$D/netv/memtile.v")
[ "$ncells" -eq "$gates" ] || fail "tile32: netlist has $ncells cell instances but abc reported $gates gates"
echo "PASS: 32x8 constant-index tile bit-blasts to $dff DFFs + $gates cells from $nodes ABC input nodes"

# ---------------------------------------------------------------------------
# 1b. N=8 tile: LEC-equivalent to the source memory under both solvers
# ---------------------------------------------------------------------------
D="$W/tile8"
map_design "$D" "$TILE" memtile -GN=8
! grep -hq "cgen_memory" "$D/netv/"*.v || fail "tile8: a native memory instance survived memory=true"
lec_both "$D" memtile
echo "PASS: 8x8 constant-index tile is LEC-equivalent to its source memory (cvc5 + lgyosys)"

# ---------------------------------------------------------------------------
# 2. read_all: lowers and LECs (bit layout: entry 0 in the low bits)
# ---------------------------------------------------------------------------
D="$W/memall"
map_design "$D" "$MEMALL" memall ""
! grep -q '"code":"memory-unlowered"' "$D/diag.jsonl" || fail "memall: read_all memory was NOT bit-blasted: $(grep memory-unlowered "$D/diag.jsonl")"
! grep -hq "cgen_memory\|_data\b" "$D/netv/"*.v || fail "memall: a native array survived memory=true"
dff=$(grep -c "^\s*DFFx1 " "$D/netv/memall.v")
[ "$dff" -eq 28 ] || fail "memall: expected 28 storage DFF cells (4 x 7), got $dff"
# cvc5 bound 3: a 3-cycle bounded proof from reset already covers a write
# followed by the whole-array read (0.2 s); the default bound 6 spends ~2 min on
# the 28-bit whole-array compare per unrolled cycle.
lec_both "$D" memall 3
echo "PASS: whole-array read_all memory bit-blasts and is LEC-equivalent (cvc5 + lgyosys)"

# ---------------------------------------------------------------------------
# 3. memory=false and memory_max_bits keep a memory native
# ---------------------------------------------------------------------------
D="$W/tile8_off"
map_design "$D" "$TILE" memtile -GN=8 --set pass.abc.memory=false
grep -hq "cgen_memory" "$D/netv/"*.v || fail "memory=false: memory was not kept as a native instance"
echo "PASS: memory=false keeps the memory a native instance"

D="$W/tile32_max"
map_design "$D" "$TILE" memtile -GN=32 --set pass.abc.memory_max_bits=255
grep -q '"code":"memory-max-bits"' "$D/diag.jsonl" || fail "memory_max_bits=255: no memory-max-bits note for a 256-bit memory: $(cat "$D/diag.jsonl")"
grep -q "32 x 8 = 256 bits" "$D/diag.jsonl" || fail "memory_max_bits note does not name the memory size: $(grep memory-max-bits "$D/diag.jsonl")"
grep -hq "cgen_memory" "$D/netv/"*.v || fail "memory_max_bits=255: 256-bit memory was bit-blasted anyway"
D="$W/tile32_max0"
map_design "$D" "$TILE" memtile -GN=32 --set pass.abc.memory_max_bits=0
! grep -hq "cgen_memory" "$D/netv/"*.v || fail "memory_max_bits=0 must disable the guard"
if "$LHD" pass abc --top memtile.memtile lg:"$W/tile32/lg" --emit-dir lg:"$W/bad_net" --set abc.library="$LIB" \
    --set pass.abc.memory_max_bits=lots --workdir "$W/bad_w" -q --result-json "$W/bad.json" 2>/dev/null; then
  fail "pass.abc accepted memory_max_bits=lots"
fi
echo "PASS: memory_max_bits keeps an over-limit memory native with a note; 0 disables; malformed rejected"

# ---------------------------------------------------------------------------
# 4. all-constant / wide-constant EQ maps to constant 0
# ---------------------------------------------------------------------------
cat > "$W/eqt.sv" <<'EOF'
module eqt (input logic [3:0] x, output logic y, output logic z);
  assign y = (x == 8'd100);   // 1100100b: always 0 for a 4-bit x
  assign z = (x == 8'd20);    //   10100b: always 0; low 4 bits = 4, so a narrow compare gives x == 4
endmodule
EOF
D="$W/eqt"
map_design "$D" "$W/eqt.sv" eqt ""
[ "$(cells "$D/netv/eqt.v")" -eq 0 ] || fail "eqt: a compare against a constant wider than its operand mapped to logic: $(grep -c '' "$D/netv/eqt.v") lines"
[ "$(grep -c "_const0_ " "$D/netv/eqt.v")" -eq 2 ] || fail "eqt: expected both outputs driven by constant 0: $(cat "$D/netv/eqt.v")"
echo "PASS: x[3:0] == 8'd100 maps to constant 0"

echo "PASS: pass.abc memory bit-blast (constant-address tile, read_all, memory_max_bits, const EQ)"
