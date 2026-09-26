#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Level-sensitive data latches map onto the Liberty's own transparent latch
# cells (pass/liberty/liberty_dff.cpp latch_ladder, pass/synth region_blast
# Latch_map + region_writer build_latch_cells) instead of staying native
# Latch nodes, one cell per bit, picked per enable polarity by area.
#
# Fixtures: lhd/tests/abc_latch_mix.v -- abc_latch_mix (active-high and
# active-low latches, a 1p/2p pipeline, resets the reader folds into D and the
# enable), abc_latch_gated / abc_latch_gprev (latches enabled by a
# latch-based clock gate's ICG output, directly and through a child's clock
# port -- minion's preview latch), abc_latch_cen (a computed enable) -- and lhd/tests/abc_latch_rst.prp
# (a Pyrope latch with a reset VALUE: the Latch cell's own reset_pin).
# Two hermetic libraries:
#   abc_latch_q.lib  (sky130-shaped): DLXTPx1 (GATE) / DLXTNx1 (GATE_N) picks,
#                    clear DLRTPx1 / preset DLSTPx1 reset latches; a cheaper
#                    dont_use, an isolation and a scan latch must never appear.
#   abc_latch_qn.lib (ASAP7-shaped): DHLx1 (CLK) and the QN-only DLLNx1
#                    (!CLK, stores !D: its D is inverted); no reset latch, so
#                    a reset folds into D and the enable.
# For each: no native state survives, the graph-native LEC (cvc5, gensim
# latch models) proves the netlist against the source graph, and the
# independent yosys checker (lgcheck, source RTL vs the Verilog netlist, gensim
# Verilog models appended to both, as lhdtrack's lec_netlist_verilog runs it)
# does not refute. abc_latch_cen is checked by lgcheck only: a data latch
# whose enable is mapped logic of the clock is outside the graph LEC's clock
# normalization (it answers unsupported; it must not refute), and so is
# abc_latch_gprev: a latch open while a gated clock is LOW can stay
# transparent through a whole phase, which the graph LEC's phase schedule
# refuses to model -- on the source already (unknown; it must not refute).
# Broken twins
# (wrong enable polarity, swapped D, a dropped clear/reset, an ungated
# enable) must refute, and a library without latch cells keeps the latches
# native with the precise reason.
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
SRC="$PWD/lhd/tests/abc_latch_mix.v"
PRP="$PWD/lhd/tests/abc_latch_rst.prp"
W="${TEST_TMPDIR:-/tmp/lhd_abc_latch_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"
[ -f "$PRP" ] || fail "missing fixture $PRP"
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

# synth <dir> <lib> <top> <src> <ref.v> <graph-lec: prove|norefute|skip> <lgcheck: prove|norefute>
# Synthesize, emit the netlist and the gensim models, and check both oracles.
synth() {
  local d="$1" lib="$2" top="$3" src="$4" refv="$5" glec="$6" lgc="$7"
  mkdir -p "$d"
  local in=(--reader slang --top "$top" -- "$src")
  [[ "$src" == *.prp ]] && in=(--top "$top" "$src")
  "$LHD" synth --result-json "$d/r.json" --workdir "$d/W" --emit-dir lg:"$d/net" --emit-dir verilog:"$d/netv" \
    --set synth.mapper="$MAPPER" --set synth.liberty="$lib" "${in[@]}" > "$d/synth.log" 2>&1 \
    || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
  "$LHD" pass liberty gensim "$lib" --emit-dir lg:"$d/models" --emit verilog:"$d/models.v" --workdir "$d/Wm" -q \
    --result-json "$d/g.json" > "$d/gensim.log" 2>&1 || fail "$d: gensim -> $(cat "$d/g.json")"
  "$LHD" lec --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/models" --top "$top" --workdir "$d/Wlec" -q \
    --result-json "$d/lec.json" > "$d/lec.log" 2>&1
  if [ "$glec" = prove ]; then
    grep -q '"verdict":"proven"' "$d/lec.json" || fail "$d: graph LEC did not prove: $(cat "$d/lec.json")"
  elif [ "$glec" = norefute ]; then
    ! grep -q '"verdict":"refuted"' "$d/lec.json" || fail "$d: graph LEC refuted: $(cat "$d/lec.json")"
  fi
  { cat "$d/net.v"; echo; cat "$d/models.v"; } > "$d/impl.v"
  { cat "$refv"; echo; cat "$d/models.v"; } > "$d/ref.v"
  local v
  v="$(lgcheck "$d/impl.v" "$d/ref.v" "$top" "$d/lgc")"
  case "$lgc:$v" in
    prove:proven | norefute:proven | norefute:inconclusive) ;;
    *) fail "$d: lgcheck on the Verilog netlist: $v ($(tail -3 "$d/lgc/lgcheck.log"))" ;;
  esac
}

# mapped <dir>: every latch became a cell -- no native state, no latch report.
mapped() {
  ! grep -q '"latch-native"' "$1/synth.log" || fail "$1: a latch stayed native: $(grep -o '"latch-native[^}]*' "$1/synth.log" | head -1)"
  ! grep -q "always_latch\|always @" "$1/net.v" || fail "$1: native state survived: $(grep 'always_latch\|always @' "$1/net.v")"
  grep -q '"native_state_nodes":0' "$1/r.json" || fail "$1: the STA still sees native state"
}

# The Pyrope fixture's golden Verilog (lgcheck reads Verilog on both sides).
"$LHD" compile -q --top abc_latch_rst "$PRP" --emit-dir verilog:"$W/rst_v" --workdir "$W/Wrst" > "$W/rst_c.log" 2>&1 \
  || fail "compile $PRP"
cat "$W/rst_v/"*.v > "$W/rst_ref.v"

run_lib() {
  local tag="$1" lib="$PWD/lhd/tests/abc_latch_$1.lib"
  synth "$W/$tag/mix" "$lib" abc_latch_mix "$SRC" "$SRC" prove prove && mapped "$W/$tag/mix"
  synth "$W/$tag/gated" "$lib" abc_latch_gated "$SRC" "$SRC" prove norefute && mapped "$W/$tag/gated"
  synth "$W/$tag/gprev" "$lib" abc_latch_gprev "$SRC" "$SRC" norefute norefute && mapped "$W/$tag/gprev"
  synth "$W/$tag/cen" "$lib" abc_latch_cen "$SRC" "$SRC" norefute prove && mapped "$W/$tag/cen"
  synth "$W/$tag/rst" "$lib" abc_latch_rst "$PRP" "$W/rst_ref.v" prove prove && mapped "$W/$tag/rst"
}

run_lib q &
p1=$!
run_lib qn &
p2=$!
# Fallback: a library without latch cells keeps every latch native, naming why.
# (lgcheck only: the native netlist drives the active-low latches' enable from
# a mapped inverter of the clock, which the graph LEC falsely refutes -- the
# same netlist the pre-latch-cell flow wrote; the cell mapping above avoids it
# by taking the clock natively on an active-low cell.)
(
  synth "$W/none" "$PWD/lhd/tests/abc_icg_q.lib" abc_latch_mix "$SRC" "$SRC" skip prove
  grep -q '"latch-native".*latch(es) kept native — the Liberty has no usable transparent latch cell' "$W/none/synth.log" \
    || fail "none: the latches must stay native, naming the missing latch cell"
  grep -q "always_latch" "$W/none/net.v" || fail "none: the latches must stay native"
) &
p3=$!
wait "$p1" || exit 1
wait "$p2" || exit 1
wait "$p3" || exit 1

N="$W/q/mix/net.v"
# a and p2 (enable clk), plus r and s: the reader folds their resets into D
# and an enable (`rst | clk`, `!rst_n | !clk`) that is mapped logic, active high
[ "$(count "$N" '^DLXTPx1 ')" = 16 ] || fail "q: expected 16 DLXTPx1 (a, p2, r, s), got $(grep -c '^DLXTPx1 ' "$N")"
# b and p1 (enable !clk): the active-low cell, its GATE_N straight from clk
[ "$(count "$N" '^DLXTNx1 ')" = 8 ] || fail "q: expected 8 DLXTNx1 (b, p1), got $(grep -c '^DLXTNx1 ' "$N")"
[ "$(grep -A3 '^DLXTNx1 ' "$N" | grep -c '\.GATE_N(clk)')" = 8 ] || fail "q: the active-low cells must take clk natively"
! grep -q "^DLXTPx0 \|^ISOLATCHx1 \|^SDLXTPx1 " "$W"/q/*/net.v || fail "q: a dont_use / isolation / scan latch was used"
# the Pyrope reset latch: 0110 -> clear, preset, preset, clear cells
N="$W/q/rst/net.v"
[ "$(count "$N" '^DLRTPx1 ')" = 2 ] && [ "$(count "$N" '^DLSTPx1 ')" = 2 ] \
  || fail "q: the reset latch must map to 2 clear + 2 preset cells: $(grep '^DL' "$N")"
# the gated latches: enabled straight by the ICG output (the child's port too)
N="$W/q/gated/net.v"
[ "$(count "$N" '^DLCLKPx1 ')" = 1 ] || fail "q: the clock gate must map to its ICG cell"
[ "$(grep -A3 '^DLXTPx1 ' "$N" | grep -c '\.GATE(en_l__icg')" = 4 ] || fail "q: g's cells must be enabled by the ICG output"
N="$W/q/gprev/net.v"
[ "$(count "$N" '^DLCLKPx1 ')" = 1 ] || fail "q: the preview's clock gate must map to its ICG cell"
[ "$(grep -A3 '^DLXTNx1 ' "$N" | grep -c '\.GATE_N(en_l__icg')" = 4 ] || fail "q: the preview cells must take the ICG output"
echo "PASS: sky130-shaped latch cells (polarity picks, clear/preset reset latches, dont_use/isolation/scan skipped), graph LEC proven, lgcheck not refuted"

N="$W/qn/mix/net.v"
[ "$(count "$N" '^DHLx1 ')" = 16 ] || fail "qn: expected 16 DHLx1 (a, p2, r, s), got $(grep -c '^DHLx1 ' "$N")"
[ "$(count "$N" '^DLLNx1 ')" = 8 ] || fail "qn: expected 8 DLLNx1 (b, p1), got $(grep -c '^DLLNx1 ' "$N")"
# the QN-only low latch stores !D: each takes an inverter on D (or its folded D PO)
[ "$(grep -c '__dinv' "$N")" -ge 8 ] || fail "qn: the QN latch cells of b and p1 must have their D inverted"
! grep -q "^DLLx1 " "$N" || fail "qn: the dearer Q-output low latch was used"
N="$W/qn/rst/net.v"
[ "$(count "$N" '^DHLx1 ')" = 4 ] || fail "qn: the reset latch must fold into 4 plain DHLx1: $(grep '^D' "$N")"
echo "PASS: ASAP7-shaped latch cells (QN low latch with D inverted, resets folded), graph LEC proven, lgcheck not refuted"
echo "PASS: fallback (a library without latch cells) keeps the latches native with the precise reason (lgcheck proves)"

# neg <tag> <top> <name> <sed> <cvc5|lgcheck> [prp]: the source with one latch
# broken must refute against the <tag>/<top> netlist.
neg() {
  local tag="$1" top="$2" what="$3" expr="$4" solver="$5" base="$SRC" sub
  sub="${top#abc_latch_}"
  [ "$top" = abc_latch_rst ] && base="$W/rst_ref.v"
  local d="$W/$tag/neg_$what"
  mkdir -p "$d"
  sed "$expr" "$base" > "$d/src.v"
  cmp -s "$base" "$d/src.v" && fail "$tag/$what: the broken twin changed nothing"
  if [ "$solver" = cvc5 ]; then
    "$LHD" compile -q --reader slang --top "$top" --emit-dir lg:"$d/ref" --workdir "$d/Wc" -- "$d/src.v" > "$d/c.log" 2>&1 \
      || fail "$tag/$what: compile"
    "$LHD" lec --impl lg:"$W/$tag/$sub/net" --ref lg:"$d/ref" --lib lg:"$W/$tag/$sub/models" --top "$top" \
      --set formal.simfail_run=false --workdir "$d/Wlec" -q --result-json "$d/r.json" > "$d/lec.log" 2>&1
    grep -q '"verdict":"refuted"' "$d/r.json" || fail "$tag/$what: broken twin was not refuted (cvc5): $(cat "$d/r.json")"
  else
    { cat "$d/src.v"; echo; cat "$W/$tag/$sub/models.v"; } > "$d/ref.v"
    local v
    v="$(lgcheck "$W/$tag/$sub/impl.v" "$d/ref.v" "$top" "$d/lgc")"
    [ "$v" = refuted ] || fail "$tag/$what: broken twin was not refuted (lgcheck): $v"
  fi
}

neg q abc_latch_mix pol "s/always_latch if (clk) a = d;/always_latch if (!clk) a = d;/" cvc5 &
p1=$!
neg qn abc_latch_mix qnpol "s/always_latch if (!clk) b = d ^ e;/always_latch if (clk) b = d ^ e;/" cvc5 &
p2=$!
neg qn abc_latch_mix swapd "s/always_latch if (!clk) p1 = d;/always_latch if (!clk) p1 = e;/" lgcheck &
p3=$!
neg q abc_latch_mix clear "s/always_latch if (rst) r = 4'b0110; else if (clk) r = e;/always_latch if (clk) r = e;/" cvc5 &
p4=$!
neg q abc_latch_rst reset "/if (reset)/d; s/else if (en)/if (en)/" lgcheck &
p5=$!
neg qn abc_latch_gated ungated "s/always_latch if (gclk) g = d;/always_latch if (clk) g = d;/" lgcheck &
p6=$!
wait "$p1" || exit 1
wait "$p2" || exit 1
wait "$p3" || exit 1
wait "$p4" || exit 1
wait "$p5" || exit 1
wait "$p6" || exit 1
echo "PASS: a wrong enable polarity / dropped clear (cvc5) and a swapped D / dropped reset / ungated enable (lgcheck) all refute"
