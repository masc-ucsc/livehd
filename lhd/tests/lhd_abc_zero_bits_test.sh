#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc.ware_zero_bits and pass.abc.memory_prune_reads (pass/synth/bit_facts.cpp):
# a module boundary hides from the mapper what the parent knows about a value.
#
#   1. zb_shift: a word select over a 40-bit vector with ONE live 8-bit lane (the
#      other four are 0 by a constant left shift). The barrel shifter is extracted
#      into a ware module; with ware_zero_bits the module is told which operand bits
#      are provably 0 and maps to far fewer cells (fixture: 2,645 -> 721 with the
#      ASAP7 library, a clear drop with the hermetic one).
#   4. zb_select: a word select by a full 32-bit index: the amount `idx * 32` has five low
#      zero bits, each a whole barrel stage the opaque module input would still build.
#   3. zb_lanes: a write-masked memory (two partial writes of one entry). The write-enable
#      lane vector of each port reaches the lowered module as its one condition bit.
#   5. zb_shared: two write ports sharing one enable cell (the parent's cell is dropped once).
#   2. zb_mem: a lowered memory whose two read ports each observe a narrow window of
#      the word. With memory_prune_reads the bits no reader observes are cleared at
#      the memory module's output, so the read mux of those bits is not built.
# Each option is off by default (the cell counts without it are the historical ones),
# and with it on the mapped netlist is LEC-equivalent to the source through the
# graph-native and the Verilog engines.
set -u

LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
SRC=lhd/tests/abc_zero_bits.sv
W="${TEST_TMPDIR:-/tmp/lhd_abc_zero_bits_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -f "$SRC" ] || fail "missing fixture $SRC"

cells() {  # cells <netlist-dir>: standard-cell instance lines over the whole emitted directory
  cat "$1"/*.v | grep -c "^\s*\(NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1\) "
}

# map_design <dir> <top> [extra pass.abc --set ...]: compile, color, tech-map, emit the netlist Verilog.
map_design() {
  local d="$1" top="$2"
  shift 2
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }
  "$LHD" compile "$SRC" --reader slang --top "$top" --emit-dir lg:"$d/lg" --workdir "$d/w1" -q --result-json "$r" \
      || fail "compile $top -> $(cat "$r" 2>/dev/null)"
  run pass color synth --top "$top.$top" lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top "$top.$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set synth.liberty="$LIB" \
      --emit diagnostics:"$d/diag.jsonl" --workdir "$d/w3" "$@"
  cp "$r" "$d/abc.json"
  run compile lg:"$d/net" --top "$top.$top" --emit-dir verilog:"$d/netv" --workdir "$d/w6"
}

# lec_both <dir> <top>: the mapped netlist against the source logic (pass partition) with gensim cell models.
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
  "$LHD" lec --set formal.effort=proof --impl lg:"$d/net" --ref lg:"$d/re" --lib lg:"$d/models" --top "$top.$top" \
      --workdir "$d/wc5" -q --result-json "$d/lec_graph.json" &
  local graph_pid=$!
  "$LHD" lec --set formal.effort=proof --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" \
      --workdir "$d/wc" -q --result-json "$d/lec_verilog.json" &
  local verilog_pid=$!
  wait "$graph_pid" || fail "$top: graph LEC failed: $(cat "$d/lec_graph.json" 2>/dev/null)"
  grep -qE '"verdict":"proven-(full|bounded)"' "$d/lec_graph.json" \
    || fail "$top: graph LEC did not prove the mapped netlist: $(grep -o '"lec":{[^}]*}' "$d/lec_graph.json")"
  wait "$verilog_pid" || fail "$top: Verilog LEC failed: $(cat "$d/lec_verilog.json" 2>/dev/null)"
  ! grep -q '"verdict":"refuted-' "$d/lec_verilog.json" || fail "$top: Verilog LEC REFUTED the mapped netlist"
}

# Each regression target owns one independent optimization claim. A direct
# invocation still checks all cases, with synthesis serialized as before.
selected="${ZERO_BITS_CASE:-all}"
case "$selected" in all|shift|mem|select|lanes|shared) ;; *) fail "unknown ZERO_BITS_CASE: $selected" ;; esac
for claim in shift mem select lanes shared; do
  [ "$selected" = all ] || [ "$selected" = "$claim" ] || continue
  top="zb_$claim"
  case "$claim" in
    shift|select)
      map_design "$W/${claim}_off" "$top"
      map_design "$W/${claim}_on" "$top" --set pass.abc.ware_zero_bits=true
      ;;
    mem|lanes)
      map_design "$W/${claim}_off" "$top" --set pass.abc.memory=true
      map_design "$W/${claim}_on" "$top" --set pass.abc.memory=true --set pass.abc.memory_prune_reads=true
      ;;
    shared)
      map_design "$W/shared_on" "$top" --set pass.abc.memory=true --set pass.abc.memory_prune_reads=true
      ;;
  esac
  on=$(cells "$W/${claim}_on/netv")
  [ "$on" -gt 0 ] || fail "$top: no mapped cells"
  if [ "$claim" != shared ]; then
    off=$(cells "$W/${claim}_off/netv")
    [ "$off" -gt 0 ] || fail "$top: no default mapped cells"
    [ "$on" -lt "$off" ] || fail "$top: optimization did not reduce the cells (off=$off on=$on)"
    if [ "$claim" = shift ]; then
      [ "$on" -lt $((off / 2)) ] || fail "$top: ware_zero_bits did not halve the cells (off=$off on=$on)"
    fi
  fi
  lec_both "$W/${claim}_on" "$top"
  echo "PASS: $top maps to $on cells, graph/Verilog LEC-equivalent"
done
