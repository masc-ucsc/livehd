#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end equivalence of every lhd elaboration route, on the
# inou/prp/tests/equiv trivial_if pyrope/verilog golden pair:
#
#   prp      -> ln -> lg -> verilog1   (through the ln: Forest seam)
#   prp            -> lg -> verilog2   (per-file lg emission, then synth)
#   verilog0       -> lg -> verilog3   (through the lg: GraphLibrary seam)
#
#   check verilog1 vs verilog0
#   check verilog2 vs verilog0
#   check verilog3 vs verilog0
#
# All three netlists must be logically equivalent (lgcheck/yosys LEC) to the
# golden verilog0.

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
V0=inou/prp/tests/equiv/trivial_if.v
TOP='trivial_if.fun3'
W="${TEST_TMPDIR:-/tmp/lhd_equiv_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# leg 1: prp -> ln -> lg -> verilog1 (through the ln: serialization seam)
run elaborate "$PRP" --emit-dir ln:"$W/lns/" --workdir "$W/w1"
run synth ln:"$W/lns/" --emit-dir lg:"$W/lg1/" --emit verilog:"$W/v1.v" --workdir "$W/w2"

# leg 2: prp -> lg -> verilog2 (per-file lg emission, then synth from lg)
run elaborate "$PRP" --emit-dir lg:"$W/lg2/" --workdir "$W/w3"
run synth lg:"$W/lg2/" --emit verilog:"$W/v2.v" --workdir "$W/w4"

# leg 3: verilog0 -> lg -> verilog3 (through the lg: serialization seam)
run elaborate "$V0" --reader yosys --top "$TOP" --emit-dir lg:"$W/lg3/" --workdir "$W/w5"
run synth lg:"$W/lg3/" --emit verilog:"$W/v3.v" --workdir "$W/w6"

for i in 1 2 3; do
  run check --impl verilog:"$W/v$i.v" --ref verilog:"$V0" --impl-top "$TOP" --ref-top "$TOP" --workdir "$W/c$i"
  echo "check verilog$i vs verilog0: equivalent"
done

echo "PASS: all three generated netlists are equivalent to the golden verilog"
