#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass abc --set pass.abc.seq=true` (task 2a-abc
# subtask 5): technology-map a colored SEQUENTIAL design to a standard-cell
# netlist and prove it LEC-equivalent to the original logic, including flops and
# blackbox boundaries (memories + hierarchical Sub instances).
#
# Registers cross into ABC as 1-bit latches (so ABC can retime), but stay NATIVE
# LGraph flops on read-back -- they are never mapped to library DFFs (the
# vendored Liberty is purely combinational). Memories and Sub instances are
# treated as blackbox boundaries: their I/O is cut at the ABC border and they
# are rebuilt natively and reconnected. Each region's mapped netlist must be
# sequentially LEC-equivalent (yosys miter + BMC/induction, via `lhd lec --set lec.solver=lgyosys`) to
# its original-logic `partition` twin.
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

# seq_lec <fixture-basename> <top>: map seq, build the twin + models, LEC each.
seq_lec() {
  local fix="$1" top="$2"
  local prp="inou/prp/tests/pyrope/${fix}.prp"
  local d="$W/$fix"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" || fail "$* -> $(cat "$r" 2>/dev/null)"; }

  [ -f "$prp" ] || fail "missing fixture $prp"
  run compile "$prp" --top "$top" --recipe O1 --emit-dir lg:"$d/lg" --workdir "$d/w1"
  run pass color synth --top "$top" lg:"$d/lg" --workdir "$d/w2"
  # sequential tech-map: flops -> latches -> native flops; memories/Subs blackboxed
  run pass abc --top "$top" lg:"$d/lg" --emit-dir lg:"$d/net" --set abc.library="$LIB" --set pass.abc.seq=true --workdir "$d/w3"
  # the original-logic twin (same module structure)
  run pass partition --top "$top" lg:"$d/lg" --emit-dir lg:"$d/re" --workdir "$d/w4"
  run pass liberty gensim "$LIB" --emit-dir lg:"$d/models" --workdir "$d/w5"

  run compile lg:"$d/net" --top "$top" --recipe O0 --emit-dir verilog:"$d/netv" --workdir "$d/w6"
  run compile lg:"$d/models" --recipe O0 --emit-dir verilog:"$d/modelsv" --workdir "$d/w7"
  run compile lg:"$d/re" --top "$top" --recipe O0 --emit-dir verilog:"$d/rev" --workdir "$d/w8"

  # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
  grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1" "$d/netv/"*.v || fail "$fix: no standard cells in the ABC netlist"
  # flops stayed native (an always/posedge register survives, not a mapped DFF)
  grep -hq "always" "$d/netv/"*.v || fail "$fix: no sequential logic survived (flops lost?)"
  # the memory fixture must keep its memory as a blackbox (not bit-blasted away)
  if [ "$fix" = "abc_mem" ]; then
    grep -hq "cgen_memory" "$d/netv/"*.v || fail "$fix: memory was not preserved as a blackbox in the ABC netlist"
  fi

  cat "$d/netv/"*.v "$d/modelsv/"*.v > "$d/impl.v"
  cat "$d/rev/"*.v > "$d/ref.v"

  # sequential LEC: the tech-mapped netlist must equal the original logic
  run lec --set lec.solver=lgyosys --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$top" --workdir "$d/wc"
  echo "PASS: pass.abc seq tech-map LEC-equivalent for '$top'"
}

seq_lec abc_seq abc_seq.abc_seq
seq_lec abc_mem abc_mem.abc_mem
seq_lec hier_seq hier_seq.top

echo "PASS: pass.abc seq tech-map LEC-equivalent (flops + memory + Sub blackbox)"
