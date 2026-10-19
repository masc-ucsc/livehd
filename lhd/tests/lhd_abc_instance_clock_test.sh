#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A register clocked by the OUTPUT OF A MODULE INSTANCE (NVDLA's `slcg` clock-gate
# wrapper, whose output clocks a sibling block's registers once that block is
# flattened into the same region) stays a native flop with the derived-clock-native
# warning by default, because only a region input or a recognized latch+AND clock
# gate is an admissible clock source. A native flop is not a physical cell, so the
# placer refuses the whole design. With pass.abc.derived_clock=true the instance is
# a native boundary box of the region: its rebuilt output pin clocks the Liberty DFF
# cells directly.
#
# Fixture lhd/tests/abc_instance_clock.v: two instances of a flop-based clock gate
# (negedge-sampled enable ANDed with the clock) whose outputs clock two plain
# registers of the parent. Checked on the hermetic test.lib:
#   - default: the derived-clock-native diagnostic and native `always @` flops remain;
#   - pass.abc.derived_clock=true: no derived-clock-native diagnostic, no native
#     `always @` in the netlist;
#   - every bit of `ra` is a DFF whose CLK is instance ga's gclk net, every bit
#     of `rb` is clocked by gb's (and the two nets differ);
#   - the independent yosys checker (lgcheck, original RTL vs the Verilog
#     netlist, gensim models appended to both sides) does not refute (its bounded
#     check cannot close two clocks derived from instance outputs: inconclusive);
#   - broken twins of the source must refute: a changed data function and a
#     swapped clock instance (the latter is what a mis-wired clock pin looks like).
set -u

LHD="${LHD:-lhd/lhd}"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS="$PWD/inou/yosys/yosys2"
SRC="$PWD/lhd/tests/abc_instance_clock.v"
LIB="$PWD/inou/prp/tests/abc/test.lib"
TOP=abc_instance_clock
W="${TEST_TMPDIR:-/tmp/lhd_abc_instance_clock_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"
[ -f "$LIB" ] || fail "missing liberty $LIB"
[ -x "$LGCHECK" ] || fail "missing lgcheck ($LGCHECK)"

# lgcheck <impl.v> <ref.v> <dir>: prints proven|refuted|inconclusive|error.
lgcheck() {
  mkdir -p "$3"
  (cd "$3" && LGCHECK_BMC_STEPS=8 "$LGCHECK" --yosys "$YOSYS" --implementation "$1" --reference "$2" --top "$TOP" \
    --gold_reader slang --gate_reader slang > lgcheck.log 2>&1)
  case $? in
    0) echo proven ;;
    1) echo refuted ;;
    2) echo inconclusive ;;
    *) echo error ;;
  esac
}

# gclk_net <net.v> <instance>: the net the gate instance drives on .gclk.
gclk_net() {
  awk -v inst="$2" '$1 == "abc_ic_gate" && $2 == inst "(" { f = 1 } f && /\.gclk\(/ { sub(/.*\.gclk\(/, ""); sub(/\).*/, ""); print; exit }' "$1"
}

# dff_clk <net.v> <bit>: the CLK net of the DFF cell instance named \<bit>.
dff_clk() {
  awk -v name="\\\\$2" '$1 == "DFFx1" && $2 == name { f = 1; next } f && /\.CLK\(/ { sub(/.*\.CLK\(/, ""); sub(/\).*/, ""); print; exit }' "$1"
}

# Default: the registers stay native on their instance-driven clocks.
D0="$W/off"
mkdir -p "$D0"
"$LHD" synth --result-json "$D0/r.json" --reader slang --top "$TOP" --workdir "$D0/W" --emit-dir verilog:"$D0/netv" \
  --set synth.mapper=abc --set synth.liberty="$LIB" -- "$SRC" > "$D0/synth.log" 2>&1 \
  || fail "default synth -> $(cat "$D0/r.json" 2>/dev/null)"
cat "$D0/netv/"*.v > "$D0/net.v"
grep -q '"derived-clock-native"' "$D0/synth.log" || fail "default: the derived-clock-native diagnostic is missing"
grep -q 'always @' "$D0/net.v" || fail "default: expected native flops on the instance-driven clocks"

D="$W/run"
mkdir -p "$D"
"$LHD" synth --result-json "$D/r.json" --reader slang --top "$TOP" --workdir "$D/W" --emit-dir verilog:"$D/netv" \
  --set synth.mapper=abc --set synth.liberty="$LIB" --set pass.abc.derived_clock=true -- "$SRC" > "$D/synth.log" 2>&1 \
  || fail "synth -> $(cat "$D/r.json" 2>/dev/null)"
cat "$D/netv/"*.v > "$D/net.v"

! grep -q '"derived-clock-native"' "$D/synth.log" \
  || fail "a register clocked by an instance output stayed native: $(grep -o '"derived-clock-native[^}]*' "$D/synth.log" | head -1)"
! grep -q 'always @' "$D/net.v" || fail "native state survived: $(grep 'always @' "$D/net.v")"

ga="$(gclk_net "$D/net.v" ga)"
gb="$(gclk_net "$D/net.v" gb)"
[ -n "$ga" ] && [ -n "$gb" ] || fail "cannot find the gate instances' gclk nets (ga='$ga' gb='$gb')"
[ "$ga" != "$gb" ] || fail "both gates drive the same net $ga"
for i in 0 1 2 3; do
  [ "$(dff_clk "$D/net.v" "ra[$i]")" = "$ga" ] || fail "ra[$i] must be clocked by ga's gclk '$ga', got '$(dff_clk "$D/net.v" "ra[$i]")'"
  [ "$(dff_clk "$D/net.v" "rb[$i]")" = "$gb" ] || fail "rb[$i] must be clocked by gb's gclk '$gb', got '$(dff_clk "$D/net.v" "rb[$i]")'"
done

"$LHD" pass liberty gensim "$LIB" --emit verilog:"$D/models.v" --workdir "$D/Wm" -q --result-json "$D/g.json" > "$D/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$D/g.json")"
{ cat "$D/net.v"; echo; cat "$D/models.v"; } > "$D/impl.v"
{ cat "$SRC"; echo; cat "$D/models.v"; } > "$D/ref.v"
v="$(lgcheck "$D/impl.v" "$D/ref.v" "$D/lgc")"
case "$v" in
  proven | inconclusive) ;;
  *) fail "lgcheck on the Verilog netlist: $v ($(tail -3 "$D/lgc/lgcheck.log"))" ;;
esac

neg() {
  local what="$1" expr="$2" d="$D/neg_$1"
  mkdir -p "$d"
  sed "$expr" "$SRC" > "$d/src.v"
  cmp -s "$SRC" "$d/src.v" && fail "$what: the broken twin changed nothing"
  { cat "$d/src.v"; echo; cat "$D/models.v"; } > "$d/ref.v"
  v="$(lgcheck "$D/impl.v" "$d/ref.v" "$d/lgc")"
  [ "$v" = refuted ] || fail "$what: broken twin was not refuted: $v"
}
neg data "s/d1 ^ 4'd5/d1 ^ 4'd4/"
neg clock_swap "s/posedge gclk_b/posedge gclk_a/"

echo "PASS: registers clocked by instance outputs map to DFF cells wired to the instance's clock net"
