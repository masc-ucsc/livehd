#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc memory bit-blast (`pass.abc.memory=true`, explicitly enabled; pass/abc/
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
#          cell models through the default LEC engine, using both graph and
#          emitted-Verilog inputs. N=8 keeps the array proof tractable.

set -u

LHD="${LHD:-lhd/lhd}"
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
# Cell counts are taken over the WHOLE emitted netlist directory, never one
# file: a region module (`<def>__c<id>.v`) is where the cells land as soon as the
# coloring opens more than one region in a def, which the shipped `cones` default
# routinely does. What the guards below are about is the mapped design, not which
# file a cell was written to.
cells() {  # cells <netlist-dir>: standard-cell instance lines (one per instantiated Liberty cell)
  cat "$1"/*.v | grep -c "^\s*\(NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1\) "
}
dffs() {  # dffs <netlist-dir>: storage DFF cell instances
  cat "$1"/*.v | grep -c "^\s*DFFx1 "
}

# map_design <dir> <src> <top> <slang -G...> [extra pass.abc --set ...]: compile
# through slang, color, tech-map with the requested memory policy, emit the netlist
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
  "$LHD" compile "$src" --reader slang --top "$top" --emit-dir lg:"$d/lg" --workdir "$d/w1" \
      -q --result-json "$r" ${gargs[@]+"${gargs[@]}"} || fail "compile $src $gparam -> $(cat "$r" 2>/dev/null)"
  run pass color synth --top "$top.$top" lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top "$top.$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set synth.liberty="$LIB" \
      --emit diagnostics:"$d/diag.jsonl" --workdir "$d/w3" "$@"
  cp "$r" "$d/abc.json"
  run compile lg:"$d/net" --top "$top.$top" --emit-dir verilog:"$d/netv" --workdir "$d/w6"
}

# lec_both <dir> <top>: compare the original-logic twin (pass partition) +
# gensim cell models through graph and emitted-Verilog inputs using default LEC.
lec_both() {
  local d="$1" top="$2"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  run pass partition --top "$top.$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w4"
  run pass liberty gensim "$LIB" --emit-dir lg:"$d/models" --workdir "$d/w5"
  run compile lg:"$d/models" --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  run compile lg:"$d/re" --top "$top.$top" --emit-dir verilog:"$d/rev" --workdir "$d/w8"
  cat "$d/netv/"*.v "$d/modelsv/"*.v > "$d/impl.v"
  cat "$d/rev/"*.v > "$d/ref.v"
  # The two queries below share no state (separate inputs, separate workdirs,
  # separate result JSON), so they are solved side by side rather than in turn.
  # Default LEC: graph-level, netlist as IMPL (the direction mem_lower's refinements are sound in)
  "$LHD" lec --impl lg:"$d/net" --ref lg:"$d/re" --lib lg:"$d/models" --top "$top.$top" \
      --workdir "$d/wc5" -q --result-json "$d/lec_graph.json" &
  local graph_pid=$!
  # default LEC: Verilog-level (the lhdtrack lec_netlist path)
  "$LHD" lec --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" \
      --workdir "$d/wc" -q --result-json "$d/lec_verilog.json" &
  local verilog_pid=$!
  wait "$graph_pid" || fail "$top: graph LEC failed: $(cat "$d/lec_graph.json" 2>/dev/null)"
  grep -q '"verdict":"proven"' "$d/lec_graph.json" \
    || fail "$top: graph LEC did not PROVE the bit-blasted memory: $(grep -o '"lec":{[^}]*}' "$d/lec_graph.json")"
  wait "$verilog_pid" || fail "$top: Verilog LEC failed: $(cat "$d/lec_verilog.json" 2>/dev/null)"
  ! grep -q '"verdict":"refuted"' "$d/lec_verilog.json" || fail "$top: default LEC REFUTED the bit-blasted memory"
}


# Every mapping below is independent: its own output directory, its own workdirs
# and its own result JSON. Run them all concurrently up front and have each
# section wait for the one it asserts on, instead of walking them in turn. The
# BUILD target reserves the fan-out with a cpu: tag.
cat > "$W/mem1r1w.sv" <<'EOF'
module mem1r1w (
  input  logic       clk,
  input  logic       we,
  input  logic [2:0] waddr,
  input  logic [7:0] wdata,
  input  logic [2:0] raddr,
  output logic [7:0] rdata
);
  logic [7:0] mem[8];                            // 8 x 8 = 64 storage bits
  always_ff @(posedge clk) if (we) mem[waddr] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF
cat > "$W/eqt.sv" <<'EOF'
module eqt (input logic [3:0] x, output logic y, output logic z);
  assign y = (x == 8'd100);   // 1100100b: always 0 for a 4-bit x
  assign z = (x == 8'd20);    //   10100b: always 0; low 4 bits = 4, so a narrow compare gives x == 4
endmodule
EOF

# `#!/bin/bash` is bash 3.2 on macOS, which has no associative arrays, so the
# dir -> pid map is two parallel indexed arrays.
MAP_DIR=()
MAP_PID=()
map_bg() {  # map_bg <dir> <map_design args...>: start a mapping in the background
  local d="$1"
  map_design "$@" &
  MAP_DIR[${#MAP_DIR[@]}]="$d"
  MAP_PID[${#MAP_PID[@]}]=$!
}
map_wait() {  # map_wait <dir>: block until <dir>'s mapping finished (once)
  local d="$1" i
  for ((i = 0; i < ${#MAP_DIR[@]}; i++)); do
    [ "${MAP_DIR[$i]}" = "$d" ] || continue
    MAP_DIR[$i]=""  # once
    wait "${MAP_PID[$i]}" || fail "mapping for ${d##*/} failed (see the message above)"
    return 0
  done
  return 0
}

map_bg "$W/tile32"         "$TILE"          memtile -GN=32 --set pass.abc.memory=true
map_bg "$W/tile8"          "$TILE"          memtile -GN=8  --set pass.abc.memory=true
map_bg "$W/memall"         "$MEMALL"        memall  ""     --set pass.abc.memory=true
map_bg "$W/tile8_off"      "$TILE"          memtile -GN=8  --set pass.abc.memory=false
map_bg "$W/mem_auto_over"  "$W/mem1r1w.sv"  mem1r1w ""     --set pass.abc.memory_max_bits=63
map_bg "$W/mem_auto_under" "$W/mem1r1w.sv"  mem1r1w ""     --set pass.abc.memory_max_bits=64
map_bg "$W/mem_true_over"  "$W/mem1r1w.sv"  mem1r1w ""     --set pass.abc.memory=true --set pass.abc.memory_max_bits=63
map_bg "$W/tile32_max"     "$TILE"          memtile -GN=32 --set pass.abc.memory_max_bits=255
map_bg "$W/tile32_max0"    "$TILE"          memtile -GN=32 --set pass.abc.memory_max_bits=0
map_bg "$W/eqt"            "$W/eqt.sv"      eqt     ""

# ---------------------------------------------------------------------------
# 1a. N=32 tile: lowered, guards on ABC input nodes and mapped cells
# ---------------------------------------------------------------------------
D="$W/tile32"
map_wait "$D"
! grep -q '"code":"memory-unlowered"' "$D/diag.jsonl" || fail "tile32: memory was NOT bit-blasted: $(grep memory-unlowered "$D/diag.jsonl")"
! grep -hq '`include.*cgen_memory\|reg .*\[.*:.*\].*\[' "$D/netv/"*.v || fail "tile32: a native memory instance survived memory=true"
dff=$(dffs "$D/netv")
[ "$dff" -eq 256 ] || fail "tile32: expected 256 storage DFF cells (32 x 8), got $dff"
bits=256
# The memory now has its own module. Apply the lowering-size guard to that
# body; parent control/data regions remain outside its preserved boundary.
# The gate-count guard below still covers the complete emitted design.
nodes=$(python3 - "$D/abc.json" <<'PYCODE'
import json,sys
regions=json.load(open(sys.argv[1]))['qor']['regions']
memories=[r for r in regions if r['module'].startswith('cgen_memory_')]
assert memories, 'no memory implementation region was mapped'
print(sum(r['input_nodes'] for r in memories))
PYCODE
)
gates=$(metric gates "$D/abc.json")
[ -n "$nodes" ] && [ -n "$gates" ] || fail "tile32: no qor in $(cat "$D/abc.json")"
[ "$nodes" -le $((2 * bits)) ] \
  || fail "tile32: $nodes ABC input nodes for $bits storage bits (> 2/bit): the memory implementation grew beyond its node budget"
[ "$gates" -le $((12 * bits)) ] \
  || fail "tile32: $gates mapped cells for $bits storage bits (> 12/bit): write path is no longer one mux per lane"
ncells=$(cells "$D/netv")
[ "$ncells" -eq "$gates" ] || fail "tile32: netlist has $ncells cell instances but abc reported $gates gates"
echo "PASS: 32x8 constant-index tile bit-blasts to $dff DFFs + $gates cells from $nodes ABC input nodes"

# ---------------------------------------------------------------------------
# 1b. N=8 tile: LEC-equivalent to the source memory under both solvers
# ---------------------------------------------------------------------------
D="$W/tile8"
map_wait "$D"
! grep -hq '`include.*cgen_memory' "$D/netv/"*.v || fail "tile8: a native memory instance survived memory=true"
lec_both "$D" memtile
echo "PASS: 8x8 constant-index tile is LEC-equivalent to its source memory (default LEC on graphs and Verilog)"

# ---------------------------------------------------------------------------
# 2. read_all: lowers and LECs (bit layout: entry 0 in the low bits)
# ---------------------------------------------------------------------------
D="$W/memall"
map_wait "$D"
! grep -q '"code":"memory-unlowered"' "$D/diag.jsonl" || fail "memall: read_all memory was NOT bit-blasted: $(grep memory-unlowered "$D/diag.jsonl")"
! grep -hq '`include.*cgen_memory\|reg .*\[.*:.*\].*\[' "$D/netv/"*.v || fail "memall: a native array survived memory=true"
dff=$(dffs "$D/netv")
[ "$dff" -eq 28 ] || fail "memall: expected 28 storage DFF cells (4 x 7), got $dff"
# cvc5 bound 3: a 3-cycle bounded proof from reset already covers a write
# followed by the whole-array read (0.2 s); the default bound 6 spends ~2 min on
# the 28-bit whole-array compare per unrolled cycle.
lec_both "$D" memall
echo "PASS: whole-array read_all memory bit-blasts and is LEC-equivalent (default LEC on graphs and Verilog)"

# ---------------------------------------------------------------------------
# 3. the three memory modes and the auto thresholds
# ---------------------------------------------------------------------------
D="$W/tile8_off"
map_wait "$D"
grep -hq "cgen_memory" "$D/netv/"*.v || fail "memory=false: memory was not kept as a native instance"
echo "PASS: memory=false keeps the memory a native instance"

# A 2-port memory is the shape an SRAM macro CAN have, so `auto` decides it on
# size alone: native above memory_max_bits, folded below.
D="$W/mem_auto_over"
map_wait "$D"
grep -q '"code":"memory-max-bits"' "$D/diag.jsonl" || fail "auto/max_bits=63: no memory-max-bits note for a 64-bit memory: $(cat "$D/diag.jsonl")"
grep -q "8 x 8 = 64 bits" "$D/diag.jsonl" || fail "memory-max-bits note does not name the memory size: $(grep memory-max-bits "$D/diag.jsonl")"
grep -hq '`include.*cgen_memory' "$D/netv/"*.v || fail "auto/max_bits=63: the 64-bit memory was bit-blasted anyway"
D="$W/mem_auto_under"
map_wait "$D"
! grep -hq '`include.*cgen_memory' "$D/netv/"*.v || fail "auto/max_bits=64: a memory within the limit was kept native"
# ...and `true` folds it whatever the threshold says.
D="$W/mem_true_over"
map_wait "$D"
! grep -hq '`include.*cgen_memory' "$D/netv/"*.v || fail "memory=true consulted memory_max_bits"
echo "PASS: auto folds within memory_max_bits and keeps a larger memory native; true ignores the threshold"

# The 34-port tile is over the same threshold, but no macro has 34 ports, so
# `auto` folds it anyway and says why.
D="$W/tile32_max"
map_wait "$D"
grep -q '"code":"memory-ports"' "$D/diag.jsonl" || fail "auto: no memory-ports note for the 34-port tile: $(cat "$D/diag.jsonl")"
! grep -hq '`include.*cgen_memory\|reg .*\[.*:.*\].*\[' "$D/netv/"*.v || fail "auto: the 34-port tile was kept native"
echo "PASS: auto folds an over-3-port memory whatever its size"

D="$W/tile32_max0"
map_wait "$D"
! grep -hq '`include.*cgen_memory' "$D/netv/"*.v || fail "memory_max_bits=0 must lift the size limit"
if "$LHD" pass abc --top memtile.memtile lg:"$W/tile32/lg" --emit-dir lg:"$W/bad_net" --set synth.liberty="$LIB" \
    --set pass.abc.memory_max_bits=lots --workdir "$W/bad_w" -q --result-json "$W/bad.json" 2>/dev/null; then
  fail "pass.abc accepted memory_max_bits=lots"
fi
if "$LHD" pass abc --top memtile.memtile lg:"$W/tile32/lg" --emit-dir lg:"$W/bad_net2" --set synth.liberty="$LIB" \
    --set pass.abc.memory=maybe --workdir "$W/bad_w2" -q --result-json "$W/bad2.json" 2>/dev/null; then
  fail "pass.abc accepted memory=maybe"
fi
echo "PASS: memory_max_bits=0 lifts the size limit; malformed memory/memory_max_bits rejected"

# ---------------------------------------------------------------------------
# 4. all-constant / wide-constant EQ maps to constant 0
# ---------------------------------------------------------------------------
D="$W/eqt"
map_wait "$D"
[ "$(cells "$D/netv")" -eq 0 ] || fail "eqt: a compare against a constant wider than its operand mapped to logic: $(cat "$D/netv/"*.v)"
# Both outputs read constant 0: ABC's constant net, or (when satopt proved
# the compares constant before mapping) a zero literal.
for out in y z; do
  cat "$D/netv/"*.v | grep -qE "(assign +)?$out *= *(_const0_|\('sb0\)|1'b0|'sb0)" \
    || fail "eqt: expected output $out driven by constant 0: $(cat "$D/netv/"*.v)"
done
echo "PASS: x[3:0] == 8'd100 maps to constant 0"

echo "PASS: pass.abc memory bit-blast (constant-address tile, read_all, memory modes + thresholds, const EQ)"
