#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge: a memory whose WRITE PORT COMMITS ON THE FALLING EDGE must
# be gated to the FALL slot, not to slot 0.
#
# WHY THIS FIXTURE EXISTS. The first version of the memory slot gate called
# `lc::commit_class_of(n, &clocks)` with `n` a Memory. That function returns
# `std::nullopt` for every node that is not a Flop/Fflop/Latch
# (graph/latch_contract.cpp), so the class was ALWAYS empty and the slot fell
# back to 0 -- independently of the port's clock and of the cell's `posclk`.
# `single_edge_memory_slot_test` could not see it, because its memory is
# posedge-written and slot 0 is the right answer there by accident.
#
# Here the memory is `posclk=false`.  WHAT THIS TEST ASSERTS IS A REFUSAL.
#
# A negedge FLOP is lowered by retyping it to posedge AND gating it to the fall
# slot; the two together make the result single-edge.  A Memory has no such
# retype in this pass -- it touches neither `clock_pin` nor `posclk` on a
# Memory -- so gating alone would leave the cell still committing on the fall
# while also being slot-gated.  The emission then still carries `.clk(~(clock))`
# on the memory instance, i.e. a half-lowered design.  So a falling-edge
# committing memory is REFUSED BY NAME until the rebinding is implemented.
#
# This is the control that the per-port clock classification exists for.  Before
# it, `lc::commit_class_of(n, &clocks)` returned nullopt for every Memory
# (graph/latch_contract.cpp returns nullopt for anything but Flop/Fflop/Latch),
# the class was empty, and this memory was silently gated to SLOT 0 and emitted.
# Measured on this fixture: with the slot hard-coded the emission carried
# `eq_36 = (single_edge_phase == 0); and_44 = eq_36 & we;` (slot 0); with the
# per-port classification it carried `and_44 = single_edge_phase & we;`
# (slot 1).  Neither is acceptable while the clock stays inverted, which is why
# the answer here is a refusal rather than either gate.
set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

W="${TEST_TMPDIR:-}"
if [ -z "$W" ]; then
  W="$(cd "$(dirname "$0")/../.." && pwd)/generated/single_edge_negedge_mem.$$"
fi
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

cat > "$W/memneg.prp" <<'EOF'
pub mod memneg(d:U8, we:Bool, wa:U2, ra:U2) -> (rd:U8@[1], qp:U8@[1], qn:U8@[2]) {
  reg st:U8 = 0                          // posedge flop -> slot 0
  reg nf:U8:[posclk=false] = 0           // negedge flop -> slot 1: forces P=2
  reg m:[4]U8:[posclk=false] = nil       // the memory COMMITS ON THE FALL
  st = d
  nf = st
  qp = st
  qn = nf
  rd = m[ra]                             // asynchronous read: not gated
  if we {
    m[wa] = st                           // write port: must land in slot 1
  }
}
EOF

rm -rf "$W/lg_src"
"$LHD" compile "$W/memneg.prp" --top memneg --emit-dir "lg:$W/lg_src" --workdir "$W/cw" \
  >"$W/compile.log" 2>&1 || { tail -8 "$W/compile.log"; fail "compile of memneg.prp failed"; }

rm -rf "$W/lg_norm"
if "$LHD" pass single_edge --top memneg "lg:$W/lg_src" --emit-dir "lg:$W/lg_norm" \
     --workdir "$W/pw" >"$W/pass.log" 2>&1; then
  grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1
  fail "pass single_edge ACCEPTED a falling-edge-committing memory; it must refuse until the clock is rebound"
fi
grep -q "memory-unsupported" "$W/pass.log" \
  || { tail -4 "$W/pass.log"; fail "the refusal is not the memory one"; }
grep -q "commits on the FALLING edge" "$W/pass.log" \
  || { tail -4 "$W/pass.log"; fail "the refusal does not NAME the falling committing edge"; }
echo "ok: a falling-edge-committing memory is refused BY NAME, not silently gated to slot 0"

# The refusal happens BEFORE any mutation: nothing was emitted.
[ -d "$W/lg_norm" ] && [ -n "$(ls -A "$W/lg_norm" 2>/dev/null)" ] \
  && fail "the pass emitted a graph for a design it refused -- the refusal is not before mutation"
echo "ok: the design was refused before mutation (no normalized graph emitted)"

# PAIRED POSITIVE: the SAME design with a RISING memory must still lower, or a
# pass that simply refused every memory would satisfy the half above.
sed 's/reg m:\[4\]U8:\[posclk=false\] = nil/reg m:[4]U8 = nil/' "$W/memneg.prp" > "$W/mempos.prp"
sed -i 's/pub mod memneg(/pub mod mempos(/' "$W/mempos.prp"
rm -rf "$W/lg_pos"
"$LHD" compile "$W/mempos.prp" --top mempos --emit-dir "lg:$W/lg_pos" --workdir "$W/cwp" \
  >"$W/compile_pos.log" 2>&1 || { tail -6 "$W/compile_pos.log"; fail "compile of the rising control failed"; }
rm -rf "$W/lg_pos_norm"
"$LHD" pass single_edge --top mempos "lg:$W/lg_pos" --emit-dir "lg:$W/lg_pos_norm" \
  --workdir "$W/pwp" >"$W/pass_pos.log" 2>&1 \
  || { tail -6 "$W/pass_pos.log"; fail "the RISING control was refused -- the test above proves nothing"; }
grep -q "1 memory(ies) slotted" "$W/pass_pos.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass_pos.log" | head -1; fail "the rising memory was not slot-gated"; }
echo "ok: the identical design with a RISING memory still lowers and is slotted"

echo "PASS: single_edge_memory_negedge_slot_test"
