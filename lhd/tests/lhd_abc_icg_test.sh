#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Latch-based clock gates map onto the Liberty's integrated clock-gate cell
# (pass/liberty/liberty_dff.cpp icg_ladder, pass/synth region_blast "integrated
# clock gates" + region_writer icg_output), and the registers they clock map to
# ordinary DFF / clear / preset cells clocked by the cell's output instead of
# staying native flops (derived-clock-native).
#
# Fixture lhd/tests/abc_icg_mix.v (top abc_icg_mix): the prim_clk_gate shape
# (event-control latch, scan forces the clock on), the always_latch shape, a
# pre-built ICG-style module (`~CLK` enable, a tied-off test input), and a gate
# chained off another gate's output; the gated registers are plain, async-reset
# (clear and preset bits), negedge (an inverter after the cell) and on the
# chained gate. Two hermetic libraries:
#   abc_icg_q.lib  (sky130-shaped): DLCLKPx1 (CLK, GATE -> GCLK) is the pick;
#                  a cheaper dont_use cell, a latch_negedge cell and an OR-gate
#                  cell must never appear, nor the dearer scan-pin cell.
#   abc_icg_qn.lib (ASAP7-shaped): ICGx1 (CLK, ENA, SE -> GCLK), SE tied 0.
# For each: no native state survives, the graph-native LEC (cvc5, gensim models
# incl. the ICG model) proves the netlist against the source graph, and the
# independent yosys checker (lgcheck, original RTL vs the Verilog netlist, gensim
# Verilog models appended to both sides, as lhdtrack's lec_netlist_verilog runs
# it) does not refute (it proves, except that its bounded check cannot close the
# negedge register on a gated clock: inconclusive, as on the pre-ICG netlist).
# Broken twins of the source must refute: a dropped scan force and a changed
# enable function (cvc5 on graphs), an inverted gate enable, a gate taken off the
# chain and an always-on gate (lgcheck; cvc5's BMC does not close the last one
# within its budget). abc_icg_n (the `clk | ~latch` flavour) and a library
# without an ICG cell keep the gates native with the precise reason (derived-
# clock-native / icg-native), and still prove.
set -u

MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

LHD="${LHD:-lhd/lhd}"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS="$PWD/inou/yosys/yosys2"
SRC="$PWD/lhd/tests/abc_icg_mix.v"
TOP=abc_icg_mix
W="${TEST_TMPDIR:-/tmp/lhd_abc_icg_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"
[ -x "$LGCHECK" ] || fail "missing lgcheck ($LGCHECK)"

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

# lgcheck <impl.v> <ref.v> <top> <dir>: the lhdtrack lec_netlist_verilog oracle;
# prints proven|refuted|inconclusive|error.
lgcheck() {
  mkdir -p "$4"
  (cd "$4" && LGCHECK_BMC_STEPS=8 "$LGCHECK" --yosys "$YOSYS" --implementation "$1" --reference "$2" --top "$3" \
    --gold_reader slang --gate_reader slang > lgcheck.log 2>&1)
  case $? in
    0) echo proven ;;
    1) echo refuted ;;
    2) echo inconclusive ;;
    *) echo error ;;
  esac
}

# synth <dir> <lib> <top>: synthesize, emit the netlist, the gensim models and
# the graph-native LEC verdict ($d/lec.json).
synth() {
  local d="$1" lib="$2" top="$3"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top "$top" --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.mapper="$MAPPER" --set synth.liberty="$lib" -- "$SRC" > "$d/synth.log" 2>&1 \
    || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
  "$LHD" pass liberty gensim "$lib" --emit-dir lg:"$d/models" --emit verilog:"$d/models.v" --workdir "$d/Wm" -q \
    --result-json "$d/g.json" > "$d/gensim.log" 2>&1 || fail "$d: gensim -> $(cat "$d/g.json")"
  "$LHD" lec --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/models" --top "$top" --workdir "$d/Wlec" -q \
    --result-json "$d/lec.json" > "$d/lec.log" 2>&1
  grep -q '"verdict":"proven"' "$d/lec.json" || fail "$d: graph LEC did not prove: $(cat "$d/lec.json")"
  { cat "$d/net.v"; echo; cat "$d/models.v"; } > "$d/impl.v"
  { cat "$SRC"; echo; cat "$d/models.v"; } > "$d/ref.v"
  local v
  v="$(lgcheck "$d/impl.v" "$d/ref.v" "$top" "$d/lgc")"
  case "$v" in
    proven | inconclusive) ;;
    *) fail "$d: lgcheck on the Verilog netlist: $v ($(tail -3 "$d/lgc/lgcheck.log"))" ;;
  esac
}

run_lib() {
  local tag="$1" d="$W/$1"
  synth "$d" "$PWD/lhd/tests/abc_icg_$tag.lib" "$TOP"
  ! grep -q '"derived-clock-native"' "$d/synth.log" \
    || fail "$tag: a gated register stayed native: $(grep -o '"derived-clock-native[^}]*' "$d/synth.log" | head -1)"
  ! grep -q "always @\|always_latch" "$d/net.v" || fail "$tag: native state survived: $(grep 'always @\|always_latch' "$d/net.v")"
  grep -q '"native_state_nodes":0' "$d/r.json" || fail "$tag: the STA still sees native state"
}

# neg <tag> <name> <sed> <cvc5|lgcheck>: the source with one clock gate broken
# must refute against the <tag> netlist.
neg() {
  local tag="$1" what="$2" expr="$3" solver="$4" d="$W/$1/neg_$2"
  mkdir -p "$d"
  sed "$expr" "$SRC" > "$d/src.v"
  cmp -s "$SRC" "$d/src.v" && fail "$tag/$what: the broken twin changed nothing"
  if [ "$solver" = cvc5 ]; then
    "$LHD" compile -q --reader slang --top "$TOP" --emit-dir lg:"$d/ref" --workdir "$d/Wc" -- "$d/src.v" > "$d/c.log" 2>&1 \
      || fail "$tag/$what: compile"
    "$LHD" lec --impl lg:"$W/$tag/net" --ref lg:"$d/ref" --lib lg:"$W/$tag/models" --top "$TOP" \
      --set formal.simfail_run=false --workdir "$d/Wlec" -q --result-json "$d/r.json" > "$d/lec.log" 2>&1
    grep -q '"verdict":"refuted"' "$d/r.json" || fail "$tag/$what: broken twin was not refuted (cvc5): $(cat "$d/r.json")"
  else
    { cat "$d/src.v"; echo; cat "$W/$tag/models.v"; } > "$d/ref.v"
    local v
    v="$(lgcheck "$W/$tag/impl.v" "$d/ref.v" "$TOP" "$d/lgc")"
    [ "$v" = refuted ] || fail "$tag/$what: broken twin was not refuted (lgcheck): $v"
  fi
}

run_lib q &
p1=$!
run_lib qn &
p2=$!
# Fallbacks: the OR flavour, and a library with no ICG cell (abc_arst_q.lib).
(
  synth "$W/n" "$PWD/lhd/tests/abc_icg_q.lib" abc_icg_n
  grep -q 'derived-clock-native.*an OR (`clk | ~latch`' "$W/n/synth.log" \
    || fail "n: the OR-flavour gate must stay native with its reason: $(grep -o '"derived-clock-native[^}]*' "$W/n/synth.log")"
) &
p3=$!
(
  synth "$W/none" "$PWD/lhd/tests/abc_arst_q.lib" "$TOP"
  grep -q '"icg-native".*4 latch-based clock gate(s) kept as a native latch.*no usable integrated clock-gate cell' \
    "$W/none/synth.log" || fail "none: the gates must stay native, naming the missing ICG cell"
  grep -q "always_latch" "$W/none/net.v" || fail "none: the gate latches must stay native"
  ! grep -q "DLCLK\|ICGx" "$W/none/net.v" || fail "none: an ICG cell appeared without one in the library"
) &
p4=$!
wait "$p1" || exit 1
wait "$p2" || exit 1
wait "$p3" || exit 1
wait "$p4" || exit 1

N="$W/q/net.v"
[ "$(count "$N" '^DLCLKPx1 ')" = 4 ] || fail "q: expected 4 DLCLKPx1 (one per gate), got $(grep -h '^[A-Z]*CLK' "$N")"
! grep -q "^DLCLKPx0 \|^DLCLKNx1 \|^DLCLKOx1 \|^SDLCLKPx1 " "$N" || fail "q: a dont_use / negedge / OR / dearer ICG was used"
[ "$(count "$N" '^DFFRx1 \|^DFFSx1 ')" = 4 ] || fail "q: the async-reset register on a gated clock must map to clear/preset cells"
# The chained gate's clock is the first gate's output, not the region clock.
grep -A3 "^DLCLKPx1 .u2.en_l__icg" "$N" | grep -q "\.CLK(.u0.en_latch__icg" || fail "q: u2's cell must be clocked by u0's cell"
echo "PASS: sky130-shaped ICG cells (area pick, dont_use/negedge/OR cells skipped), chained gate, graph LEC proven, lgcheck not refuted"

N="$W/qn/net.v"
[ "$(count "$N" '^ICGx1 ')" = 4 ] || fail "qn: expected 4 ICGx1, got $(grep -h '^ICG' "$N")"
[ "$(grep -A4 '^ICGx1 ' "$N" | grep -c "\.SE(1'h0)")" = 4 ] || fail "qn: every ICG test pin must be tied inactive"
[ "$(count "$N" '^DFFASRNx1 ')" = 4 ] || fail "qn: the async-reset register on a gated clock must map to DFFASRNx1"
echo "PASS: ASAP7-shaped ICG cells (SE tied 0), graph LEC proven, lgcheck not refuted"
echo "PASS: fallbacks (OR-flavour gate, library without an ICG cell) stay native with the precise reason and prove"

neg q scan "s/en_latch <= en_i | scan;/en_latch <= en_i;/" cvc5 &
p1=$!
neg qn enfn "s/.en_i(en0 \& en2)/.en_i(en0)/" cvc5 &
p2=$!
neg q inv "s/assign gclk = clk \& en_l;/assign gclk = clk \& ~en_l;/" lgcheck &
p3=$!
neg qn chain "s/abc_icg_latch u2(.clk(g0)/abc_icg_latch u2(.clk(clk)/" lgcheck &
p4=$!
neg q en "s/always_latch if (!clk) en_l = en;/always_latch if (!clk) en_l = 1'b1;/" lgcheck &
p5=$!
wait "$p1" || exit 1
wait "$p2" || exit 1
wait "$p3" || exit 1
wait "$p4" || exit 1
wait "$p5" || exit 1
echo "PASS: a dropped scan force / changed enable (cvc5) and an inverted / unchained / always-on gate (lgcheck) all refute"
