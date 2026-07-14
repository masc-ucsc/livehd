#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Whole-design flatten (`pass.color flat` + `pass.abc` / `pass.partition`): a
# flat one-color-for-everything coloring must inline the instance hierarchy and
# produce EXACTLY ONE output module named after the top — no per-def wrappers,
# no __c<color> region modules — and that single flat module must stay
# LEC-equivalent to the original hierarchical logic.
#
#   prp -> lg (O1)                            hier_seq: 3-level pipeline
#   pass color flat                           (one color across the hierarchy)
#   pass abc                                  (flatten=auto fires on the flat coloring)
#   pass partition                            (flatten=auto twin: original logic, flat)
#   pass liberty gensim test.lib              (behavioral model per comb cell)
#   lec netlist+models vs twin                (sequential equivalence, lgyosys)
#   pass abc --set pass.abc.flatten=false     (escape hatch: classic per-def shape)
#
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_abc_flat_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[ -f "$LIB" ] || fail "missing liberty $LIB"

FIX=inou/prp/tests/pyrope/hier_seq.prp
TOP=hier_seq.top
[ -f "$FIX" ] || fail "missing fixture $FIX"

D="$W/flat"
mkdir -p "$D"
R="$D/r.json"
run() { "$LHD" "$@" -q --result-json "$R" || fail "$* -> $(cat "$R" 2>/dev/null)"; }

# Live (non-tombstoned) non-cell defs in a graph_library dir. `graph_io_deleted`
# lines are gid-reuse tombstones (e.g. the flatten scratch def) — not defs.
live_defs() {
  grep -E "^graph_io " "$1/library.txt" | awk '{print $3}' | grep -v "^_const" | grep -v "x1$" | sort
}

run compile "$FIX" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color flat --top "$TOP" lg:"$D/lg" --workdir "$D/w2"

# pass.abc: flatten=auto must fire on the flat coloring -> one netlist module.
run pass abc --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/net" --set abc.library="$LIB" --workdir "$D/w3"
NET_DEFS=$(live_defs "$D/net")
[ "$NET_DEFS" = "$TOP" ] || fail "flat abc netlist must hold exactly '$TOP', got: $(echo $NET_DEFS)"
echo "PASS: pass.abc + flat coloring emits a single flat netlist module"

# pass.partition twin: same flatten, original logic.
run pass partition --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/re" --workdir "$D/w4"
RE_DEFS=$(live_defs "$D/re")
[ "$RE_DEFS" = "$TOP" ] || fail "flat partition twin must hold exactly '$TOP', got: $(echo $RE_DEFS)"
echo "PASS: pass.partition + flat coloring emits a single flat twin module"

run pass liberty gensim "$LIB" --emit-dir lg:"$D/models" --workdir "$D/w5"
run compile lg:"$D/net" --top "$TOP" --recipe O0 --emit-dir verilog:"$D/netv" --workdir "$D/w6"
run compile lg:"$D/models" --recipe O0 --emit-dir verilog:"$D/modelsv" --workdir "$D/w7"
run compile lg:"$D/re" --top "$TOP" --recipe O0 --emit-dir verilog:"$D/rev" --workdir "$D/w8"

# One emitted netlist .v, real standard cells, flops mapped, hierarchy gone.
NV=$(ls "$D/netv/"*.v | wc -l | tr -d ' ')
[ "$NV" = "1" ] || fail "expected exactly one netlist .v, got $NV"
grep -hq "NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1" "$D/netv/"*.v || fail "no standard cells in the flat netlist"
grep -hq "DFFx1 " "$D/netv/"*.v || fail "flops were not mapped to DFF cells in the flat netlist"
! grep -hq "__c[0-9]" "$D/netv/"*.v || fail "a __c<color> region module leaked into the flat netlist"
! grep -hq "stage_unit\b" "$D/netv/"*.v || fail "a child module instance survived the flatten"

cat "$D/netv/"*.v "$D/modelsv/"*.v > "$D/impl.v"
cat "$D/rev/"*.v > "$D/ref.v"
run lec --set lec.solver=lgyosys --impl verilog:"$D/impl.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/wc"
echo "PASS: flat netlist LEC-equivalent to the flat original-logic twin"

# Flop-name preservation on the native read-back: abc_flat_names' registers
# carry an implicit power-on init and NO reset, so register=true (default)
# cannot map them to plain DFF cells — each must be rebuilt as ONE multi-bit
# native flop under its ORIGINAL hierarchical name (never anonymous per-bit
# __rinit/__r flops; the LEC's tier-1 state pairing leans on those names).
FIX2=inou/prp/tests/pyrope/abc_flat_names.prp
TOP2=abc_flat_names.top
[ -f "$FIX2" ] || fail "missing fixture $FIX2"
D2="$W/flatnames"
mkdir -p "$D2"
run compile "$FIX2" --top "$TOP2" --recipe O1 --emit-dir lg:"$D2/lg" --workdir "$D2/w1"
run pass color flat --top "$TOP2" lg:"$D2/lg" --workdir "$D2/w2"
run pass abc --top "$TOP2" lg:"$D2/lg" --emit-dir lg:"$D2/net" --set abc.library="$LIB" --workdir "$D2/w3"
run pass partition --top "$TOP2" lg:"$D2/lg" --emit-dir lg:"$D2/re" --workdir "$D2/w4"
run compile lg:"$D2/net" --top "$TOP2" --recipe O0 --emit-dir verilog:"$D2/netv" --workdir "$D2/w5"
run compile lg:"$D2/re" --top "$TOP2" --recipe O0 --emit-dir verilog:"$D2/rev" --workdir "$D2/w6"
grep -hq "holder.*\.r " "$D2/netv/"*.v || fail "hierarchical flop name lost in the flat netlist (expected a '<inst>.r' register)"
! grep -hq "__rinit\|__r[0-9]" "$D2/netv/"*.v || fail "anonymous __rinit/__r flop leaked (original register names must survive)"
! grep -hq "DFFx1 " "$D2/netv/"*.v || fail "an init-carrying register was mapped to a DFF cell (power-on init would be lost)"
cat "$D2/netv/"*.v "$D/modelsv/"*.v > "$D2/impl.v"
cat "$D2/rev/"*.v > "$D2/ref.v"
run lec --set lec.solver=lgyosys --impl verilog:"$D2/impl.v" --ref verilog:"$D2/ref.v" --top "$TOP2" --workdir "$D2/wc"
echo "PASS: init-carrying registers keep their hierarchical names as native flops (LEC-proven)"

# Escape hatch: flatten=false restores the classic per-def wrapper+region shape.
run pass abc --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/net_hier" --set abc.library="$LIB" \
    --set pass.abc.flatten=false --workdir "$D/w9"
HIER_DEFS=$(live_defs "$D/net_hier")
echo "$HIER_DEFS" | grep -q "__c" || fail "flatten=false must keep per-def __c<color> modules, got: $(echo $HIER_DEFS)"
N_HIER=$(echo "$HIER_DEFS" | wc -l | tr -d ' ')
[ "$N_HIER" -gt 2 ] || fail "flatten=false must keep the per-def decomposition, got only: $(echo $HIER_DEFS)"
echo "PASS: pass.abc.flatten=false keeps the classic per-def decomposition"

echo "PASS: whole-design flatten (color flat -> single netlist module, LEC-proven)"
