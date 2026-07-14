#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the sequential `lhd pass abc` knobs (task 2a-abc subtask 5):
# technology-map a colored SEQUENTIAL design to a standard-cell netlist and prove
# it LEC-equivalent to the original logic, exercising both register-mapping modes
# and both memory-mapping modes.
#
#   pass.abc.register=true   flops -> library DFF cells (DFFx1 in the test lib)
#   pass.abc.register=false  flops kept native (`always @(posedge)`)
#   pass.abc.memory=true     memory bit-blasted into a DFF array + mux gates
#   pass.abc.memory=false    memory kept as a native boundary instance
#
# Registers cross into ABC as 1-bit latches (so ABC can optimize the surrounding
# logic); on read-back register=true maps each latch to a plain DFF cell (a bit
# carrying a power-on init stays native so the value survives), register=false
# rebuilds a native flop. Sub instances are blackbox boundaries; a memory is
# either a boundary (memory=false) or lowered to flops+mux gates (memory=true).
# Each mode's mapped netlist must be sequentially LEC-equivalent (yosys miter +
# BMC/induction, via `lhd lec --set lec.solver=lgyosys`) to its `partition` twin.
#
#   prp -> lg (O1)
#   pass color synth                       (the abc driver coloring)
#   pass abc --set pass.abc.seq=true        (partition + ABC seq tech-map)
#   pass partition                          (same module structure, original logic)
#   pass liberty gensim test.lib            (behavioral model per comb cell)
#   cgen net + models -> impl.v ; cgen re -> ref.v
#   lhd lec --set lec.solver=lgyosys (impl vs ref): must be sequentially LEC-equivalent
#
# LEC soundness of this `lhd lec` / gensim pipeline (a corrupted reference must
# FAIL) is covered by the combinational lhd_abc_test's negative control. A
# sequential negative control is intentionally omitted here: disproving
# sequential inequivalence drives yosys onto a slow temporal-induction search,
# and lgcheck already pins reset to avoid vacuous sequential passes. This test's
# guards against a vacuous pass are the structural checks below (real standard
# cells AND a surviving sequential `always` block per fixture) plus three
# independent positive proofs (flops, memory, hierarchy).
#
# Fixtures (inou/prp/tests/pyrope): abc_seq (two flat registers + comb cones),
# abc_mem (1rd/1wr register-array memory as a blackbox), hier_seq (3-level
# pipeline: top -> stage_unit x2 -> delayer, flops + Sub blackbox boundaries).
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_abc_seq_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -f "$LIB" ] || fail "missing liberty $LIB"

# run_abc_lec <fix> <top> <register> <memory>: tech-map with the given knobs,
# build the original-logic twin + gensim models, and prove the netlist equivalent.
# Leaves the netlist verilog dir in the global NETV for the caller's structural asserts.
NETV=""
run_abc_lec() {
  local fix="$1" top="$2" reg="$3" mem="$4"
  local prp="inou/prp/tests/pyrope/${fix}.prp"
  local d="$W/${fix}_r${reg}_m${mem}"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }

  [ -f "$prp" ] || fail "missing fixture $prp"
  run compile "$prp" --top "$top" --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1"
  run pass color synth --top "$top" lg:"$d/lg" --workdir "$d/w2"
  run pass abc --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$LIB" \
      --set pass.abc.register="$reg" --set pass.abc.memory="$mem" --workdir "$d/w3"
  # the original-logic twin (same module structure)
  run pass partition --top "$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w4"
  run pass liberty gensim "$LIB" --emit-dir lg:"$d/models" --workdir "$d/w5"

  run compile lg:"$d/net" --top "$top" --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
  run compile lg:"$d/models" --recipe O0 --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  run compile lg:"$d/re" --top "$top" --recipe O0 --emit-dir verilog:"$d/rev" --workdir "$d/w8"

  # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
  grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1" "$d/netv/"*.v \
    || fail "$fix[reg=$reg,mem=$mem]: no standard cells in the ABC netlist"

  cat "$d/netv/"*.v "$d/modelsv/"*.v > "$d/impl.v"
  cat "$d/rev/"*.v > "$d/ref.v"

  # LEC: the tech-mapped netlist must equal the original logic in every mode
  run lec --set lec.solver=lgyosys --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" --workdir "$d/wc"
  NETV="$d/netv"
}

has() { grep -hq "$2" "$1/"*.v; }

# register=true: flops map to library DFF cells (abc_seq/hier_seq are init-less,
# so every flop becomes a DFFx1 cell -- no behavioral `always @(posedge)` left).
run_abc_lec abc_seq abc_seq.abc_seq true false
has "$NETV" "DFFx1 " || fail "abc_seq register=true: flops were not mapped to DFF cells"
! has "$NETV" "posedge" || fail "abc_seq register=true: a behavioral flop survived (expected all DFF cells)"
echo "PASS: register=true maps flops to DFF cells (abc_seq)"

run_abc_lec hier_seq hier_seq.top true false
has "$NETV" "DFFx1 " || fail "hier_seq register=true: flops were not mapped to DFF cells"
echo "PASS: register=true maps flops to DFF cells across hierarchy (hier_seq)"

# register=false: flops kept native (`always @(posedge)`), never a DFF cell.
run_abc_lec abc_seq abc_seq.abc_seq false false
has "$NETV" "posedge" || fail "abc_seq register=false: no native flop survived (flops lost?)"
! has "$NETV" "DFFx1 " || fail "abc_seq register=false: unexpected DFF cell (flop should stay native)"
echo "PASS: register=false keeps flops native (abc_seq)"

# memory=false: the memory stays a native boundary instance (not bit-blasted).
run_abc_lec abc_mem abc_mem.abc_mem true false
has "$NETV" "cgen_memory" || fail "abc_mem memory=false: memory not preserved as a native instance"
echo "PASS: memory=false keeps the memory as a native instance (abc_mem)"

# memory=true: the memory is bit-blasted into gates -- no memory instance remains.
run_abc_lec abc_mem abc_mem.abc_mem true true
! has "$NETV" "cgen_memory" || fail "abc_mem memory=true: memory was not bit-blasted"
echo "PASS: memory=true bit-blasts the memory to gates (abc_mem)"

echo "PASS: pass.abc register/memory tech-map LEC-equivalent (DFF cells, native flops, memory bit-blast + boundary)"
