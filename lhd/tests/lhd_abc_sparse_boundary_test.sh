#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A wide native wiring boundary can expose thousands of bits while mapped
# logic consumes only a sparse subset.  The ABC interface and its readback must
# materialize just that demand; eager expansion made this 4k-bit fixture create
# thousands of selectors (and real CDC designs create millions).

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
FIX=lhd/tests/abc_sparse_boundary.prp
TOP=abc_sparse_boundary.top
W="${TEST_TMPDIR:-/tmp/lhd_abc_sparse_boundary_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run() {
  "$LHD" "$@" -q --result-json "$W/result.json" || fail "$* -> $(cat "$W/result.json" 2>/dev/null)"
}

run compile "$FIX" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w_compile"
run pass color flat --top "$TOP" lg:"$W/lg" --workdir "$W/w_color"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set abc.library="$LIB" --workdir "$W/w_abc"

TREE=$("$LHD" tool tree lg:"$W/net" --top "$TOP") || fail "could not inspect mapped graph"
NODES=$(printf '%s\n' "$TREE" | sed -n '1s/.*\[\([0-9][0-9]*\) nodes\].*/\1/p')
[ -n "$NODES" ] || fail "could not parse mapped node count: $TREE"
[ "$NODES" -lt 1000 ] || fail "sparse 4096-bit boundary expanded to $NODES mapped nodes"

# Correctness guard: the compact mapped result remains equivalent to the same
# flattened source graph.  Generate behavioral models for the tiny test
# Liberty so the independent Yosys LEC understands the mapped cells.
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/ref" --workdir "$W/w_partition"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_models"
run compile lg:"$W/net" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/netv" --workdir "$W/w_netv"
# ABC's compact scalar boundary selectors must stay bit-selects in Verilog;
# spelling each one as a full-width shift makes downstream Yosys build a 4096
# bit shifter before truncating it to one bit.
if grep -Rh '>>>' "$W/netv" >/dev/null; then
  fail "sparse scalar boundary selector emitted as a full-width shift"
fi
run compile lg:"$W/ref" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/refv" --workdir "$W/w_refv"
run compile lg:"$W/models" --recipe O0 --emit-dir verilog:"$W/modelsv" --workdir "$W/w_modelsv"
cat "$W/netv/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/refv/"*.v > "$W/ref.v"
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/w_lec"

echo "PASS: ABC materializes only demanded bits of wide native boundaries ($NODES mapped nodes, LEC-proven)"
