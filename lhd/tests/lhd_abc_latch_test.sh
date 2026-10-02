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
# For each: no native state survives, every state bit is exactly one cell, the
# graph-native LEC (cvc5, gensim latch models) proves the netlist against the
# source graph, and the
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
# native with the precise reason -- the active-low ones enabled by a mapped
# inverter cell of the clock, which the graph LEC must see through (prove; its
# wrong-polarity / swapped-D twins refute).
#
# MAPPER=usyn (native USYN, default tmap=abc): a transparent latch is never a
# USYN endpoint (the schema-5 report counts 0 register bits / 0 endpoints on
# every fixture) and stays native through logical synthesis; the ABC tmap
# provider then maps it onto the same Liberty latch cells, or keeps it native
# with the same latch-native reason. Every semantic claim holds for both
# mappers: the proofs, the broken twins, no native state, one cell per state
# bit, the reset-latch picks and the skipped dont_use/isolation/scan cells.
# ABC-only (gated on MAPPER=abc): the per-polarity picks (DLXTNx1 GATE_N /
# DLLNx1 with an inverted D; the tmap realizes an active-low enable with an
# inverter on a high-enable cell) and the integrated clock-gate cells
# (DLCLKPx1/ICGx1 enabling the gated latches; USYN's native ICG barrier leaves
# the gate's latch to the tmap, which maps it onto a data latch cell).
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
DEFERRED="$W/deferred_failures"
: >"$DEFERRED"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
# defer <msg>: a failed check that must fail the test but not stop the case
# job, so the job's other oracles, cell checks and broken twins still run.
defer() {
  echo "FAIL: $*" >&2
  echo "$*" >>"$DEFERRED"
}
[ -f "$SRC" ] || fail "missing fixture $SRC"
[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -x "$LGCHECK" ] || fail "missing lgcheck ($LGCHECK)"

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

# lgcheck <impl.v> <ref.v> <top> <dir> [bmc steps, default 8]: the lhdtrack
# lec_netlist_verilog oracle; prints proven|refuted|inconclusive|error.
lgcheck() {
  mkdir -p "$4"
  (cd "$4" && LGCHECK_BMC_STEPS="${5:-8}" "$LGCHECK" --yosys "$YOSYS" --implementation "$1" --reference "$2" --top "$3" \
    --gold_reader slang --gate_reader slang > lgcheck.log 2>&1)
  case $? in
    0) echo proven ;;
    1) echo refuted ;;
    2) echo inconclusive ;;
    *) echo error ;;
  esac
}
# A broken twin must be REFUTED: a counterexample within fewer steps is still a
# counterexample, so the shorter bound can only make a twin harder to refute
# (the positive checks keep the full 8 steps).
NEG_BMC_STEPS=4

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
    grep -q '"verdict":"proven"' "$d/lec.json" || defer "$d: graph LEC did not prove: $(cat "$d/lec.json")"
  elif [ "$glec" = norefute ]; then
    ! grep -q '"verdict":"refuted"' "$d/lec.json" || defer "$d: graph LEC refuted: $(cat "$d/lec.json")"
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

# Every latch / ICG cell either library offers (the dont_use, isolation and scan
# ones included, so a wrong pick still counts as a state cell).
STATE_CELL='^(S?DL[A-Z]+|DHL|ISOLATCH|ICG)x[0-9]+ '

# mapped <dir> <state bits>: every latch became a cell -- no native state, no
# latch report, exactly one cell per state bit and no invented flop.
mapped() {
  ! grep -q '"latch-native"' "$1/synth.log" || fail "$1: a latch stayed native: $(grep -o '"latch-native[^}]*' "$1/synth.log" | head -1)"
  ! grep -q "always_latch\|always @" "$1/net.v" || fail "$1: native state survived: $(grep 'always_latch\|always @' "$1/net.v")"
  grep -q '"native_state_nodes":0' "$1/r.json" || fail "$1: the STA still sees native state"
  state_cells "$1" "$2"
}

# state_cells <dir> <n>: exactly <n> latch/ICG cells and no flop cell (the
# fixtures have no edge-triggered state); under usyn, the native report also
# counts no register bit and no endpoint (a transparent latch is never one).
state_cells() {
  local n
  n=$(grep -cE "$STATE_CELL" "$1/net.v")
  [ "$n" = "$2" ] || fail "$1: expected $2 latch/ICG cells (one per state bit), got $n: $(grep -E "$STATE_CELL" "$1/net.v" | awk '{print $1}' | sort | uniq -c | tr '\n' ' ')"
  ! grep -qE '^S?DFF[A-Za-z0-9]* ' "$1/net.v" || fail "$1: a flop cell appeared in a latch-only design"
  if [ "$MAPPER" = usyn ]; then
    local v
    v=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); t=d["totals"]; print(d["schema_version"],d["kind"],d["output"],t["register_bits"],t["eligible_endpoints"])' \
      "$1/W/synth/qor.json.usyn.json" 2>&1)
    [ "$v" = "5 usyn mapped-cmos 0 0" ] || fail "$1: USYN report (schema kind output register_bits endpoints) = $v"
  fi
}

# q_clean <dir>: the sky130-shaped library's dont_use / isolation / scan latches never appear.
q_clean() {
  ! grep -q "^DLXTPx0 \|^ISOLATCHx1 \|^SDLXTPx1 " "$1/net.v" || fail "$1: a dont_use / isolation / scan latch was used"
}

# neg <tag> <top> <name> <sed> <cvc5|lgcheck>: the source with one latch
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
    v="$(lgcheck "$W/$tag/$sub/impl.v" "$d/ref.v" "$top" "$d/lgc" "$NEG_BMC_STEPS")"
    [ "$v" = refuted ] || fail "$tag/$what: broken twin was not refuted (lgcheck): $v"
  fi
}

# wait_all <pid>...: wait for every job; fails if any failed.
wait_all() {
  local p rc=0
  for p in "$@"; do wait "$p" || rc=1; done
  return $rc
}

lib_of() { echo "$PWD/lhd/tests/abc_latch_$1.lib"; }

# One background job per synthesized case: synthesis, both oracles, the cell
# checks, then the broken twins that need that netlist.
case_mix() {
  local tag=$1 d="$W/$1/mix" N="$W/$1/mix/net.v" p1 p2
  synth "$d" "$(lib_of "$tag")" abc_latch_mix "$SRC" "$SRC" prove prove
  mapped "$d" 24  # a, b, p1, p2, r, s: 4 bits each
  if [ "$tag" = q ]; then
    q_clean "$d"
    if [ "$MAPPER" = abc ]; then
      # a and p2 use plain high-enable cells. The reader preserves r and s's
      # resets, so their bits use the library's clear/preset cells.
      [ "$(count "$N" '^DLXTPx1 ')" = 8 ] || fail "q: expected 8 DLXTPx1 (a, p2), got $(grep -c '^DLXTPx1 ' "$N")"
      [ "$(count "$N" '^DLRTPx1 ')" = 4 ] && [ "$(count "$N" '^DLSTPx1 ')" = 4 ] \
        || fail "q: r and s must use 4 clear and 4 preset latch cells"
      # b and p1 (enable !clk): the active-low cell, its GATE_N straight from clk
      [ "$(count "$N" '^DLXTNx1 ')" = 8 ] || fail "q: expected 8 DLXTNx1 (b, p1), got $(grep -c '^DLXTNx1 ' "$N")"
      [ "$(grep -A3 '^DLXTNx1 ' "$N" | grep -c '\.GATE_N(clk)')" = 8 ] || fail "q: the active-low cells must take clk natively"
    fi
    neg q abc_latch_mix pol "s/always_latch if (clk) a = d;/always_latch if (!clk) a = d;/" cvc5 &
    p1=$!
    neg q abc_latch_mix clear "s/always_latch if (rst) r = 4'b0110; else if (clk) r = e;/always_latch if (clk) r = e;/" cvc5 &
    p2=$!
    wait_all "$p1" "$p2" || exit 1
  else
    if [ "$MAPPER" = abc ]; then
      # With no reset cells, r and s fold reset into their data and enable,
      # retaining the source enable's natural polarity.
      [ "$(count "$N" '^DHLx1 ')" = 12 ] || fail "qn: expected 12 DHLx1 (a, p2, r), got $(grep -c '^DHLx1 ' "$N")"
      [ "$(count "$N" '^DLLNx1 ')" = 12 ] || fail "qn: expected 12 DLLNx1 (b, p1, s), got $(grep -c '^DLLNx1 ' "$N")"
      # the QN-only low latch stores !D: each takes an inverter on D (or its folded D PO)
      [ "$(grep -c '__dinv' "$N")" -ge 8 ] || fail "qn: the QN latch cells of b and p1 must have their D inverted"
    fi
    ! grep -q "^DLLx1 " "$N" || fail "qn: the dearer Q-output low latch was used"
    neg qn abc_latch_mix qnpol "s/always_latch if (!clk) b = d ^ e;/always_latch if (clk) b = d ^ e;/" cvc5 &
    p1=$!
    neg qn abc_latch_mix swapd "s/always_latch if (!clk) p1 = d;/always_latch if (!clk) p1 = e;/" lgcheck &
    p2=$!
    wait_all "$p1" "$p2" || exit 1
  fi
}

# the gated latches: enabled by the ICG output (the child's port too): 4 data
# latch bits + the gate's own latch
case_gated() {
  local tag=$1 d="$W/$1/gated" N="$W/$1/gated/net.v"
  synth "$d" "$(lib_of "$tag")" abc_latch_gated "$SRC" "$SRC" prove norefute
  mapped "$d" 5
  if [ "$tag" = q ]; then
    q_clean "$d"
    if [ "$MAPPER" = abc ]; then
      [ "$(count "$N" '^DLCLKPx1 ')" = 1 ] || fail "q: the clock gate must map to its ICG cell"
      [ "$(grep -A3 '^DLXTPx1 ' "$N" | grep -c '\.GATE(en_l__icg')" = 4 ] || fail "q: g's cells must be enabled by the ICG output"
    fi
  else
    neg qn abc_latch_gated ungated "s/always_latch if (gclk) g = d;/always_latch if (clk) g = d;/" lgcheck
  fi
}

case_gprev() {
  local tag=$1 d="$W/$1/gprev" N="$W/$1/gprev/net.v"
  synth "$d" "$(lib_of "$tag")" abc_latch_gprev "$SRC" "$SRC" norefute norefute
  mapped "$d" 5
  if [ "$tag" = q ]; then
    q_clean "$d"
    if [ "$MAPPER" = abc ]; then
      [ "$(count "$N" '^DLCLKPx1 ')" = 1 ] || fail "q: the preview's clock gate must map to its ICG cell"
      [ "$(grep -A3 '^DLXTNx1 ' "$N" | grep -c '\.GATE_N(en_l__icg')" = 4 ] || fail "q: the preview cells must take the ICG output"
    fi
  fi
}

case_cen() {
  local tag=$1 d="$W/$1/cen"
  synth "$d" "$(lib_of "$tag")" abc_latch_cen "$SRC" "$SRC" norefute prove
  mapped "$d" 4
  [ "$tag" = q ] && q_clean "$d"
  return 0
}

case_rst() {
  local tag=$1 d="$W/$1/rst" N="$W/$1/rst/net.v"
  synth "$d" "$(lib_of "$tag")" abc_latch_rst "$PRP" "$W/rst_ref.v" prove prove
  mapped "$d" 4
  if [ "$tag" = q ]; then
    q_clean "$d"
    # the Pyrope reset latch: 0110 -> clear, preset, preset, clear cells
    [ "$(count "$N" '^DLRTPx1 ')" = 2 ] && [ "$(count "$N" '^DLSTPx1 ')" = 2 ] \
      || fail "q: the reset latch must map to 2 clear + 2 preset cells: $(grep '^DL' "$N")"
    neg q abc_latch_rst reset "/if (reset)/d; s/else if (en)/if (en)/" lgcheck
  else
    [ "$(count "$N" '^DHLx1 ')" = 4 ] || fail "qn: the reset latch must fold into 4 plain DHLx1: $(grep '^D' "$N")"
  fi
}

# Fallback: a library without latch cells keeps every latch native, naming why.
# The native active-low latches (b, p1) are enabled by a mapped INVERTER CELL
# of the clock: the graph LEC must see through it (pass/single_edge/proof_prep
# inline_clock_lib_cells walks a latch's enable) and prove, where it used to
# read the enable as data and falsely refute.
case_none() {
  local d="$W/none/mix" p1 p2
  synth "$d" "$PWD/lhd/tests/abc_icg_q.lib" abc_latch_mix "$SRC" "$SRC" prove prove
  grep -q '"latch-native".*latch(es) kept native — the Liberty has no usable transparent latch cell' "$d/synth.log" \
    || fail "none: the latches must stay native, naming the missing latch cell"
  grep -q "always_latch" "$d/net.v" || fail "none: the latches must stay native"
  grep -q "^INVx1 .*" "$d/net.v" || fail "none: expected a mapped inverter cell (the enable of b/p1)"
  state_cells "$d" 0
  # the native fallback: seeing through the inverter-cell enable must not hide a
  # wrong polarity or a wrong D from the graph LEC
  neg none abc_latch_mix invpol "s/always_latch if (!clk) b = d ^ e;/always_latch if (clk) b = d ^ e;/" cvc5 &
  p1=$!
  neg none abc_latch_mix invswapd "s/always_latch if (!clk) p1 = d;/always_latch if (!clk) p1 = e;/" cvc5 &
  p2=$!
  wait_all "$p1" "$p2" || exit 1
}

PIDS=()
spawn() {
  "$@" &
  PIDS+=($!)
}
# the longest chain (qn/mix and its lgcheck twin) first
spawn case_mix qn
spawn case_mix q
spawn case_none
for tag in q qn; do
  spawn case_gated "$tag"
  spawn case_gprev "$tag"
  spawn case_cen "$tag"
done
# The Pyrope fixture's golden Verilog (lgcheck reads Verilog on both sides).
"$LHD" compile -q --top abc_latch_rst "$PRP" --emit-dir verilog:"$W/rst_v" --workdir "$W/Wrst" > "$W/rst_c.log" 2>&1 \
  || fail "compile $PRP"
cat "$W/rst_v/"*.v > "$W/rst_ref.v"
spawn case_rst q
spawn case_rst qn

rc=0
for p in "${PIDS[@]}"; do wait "$p" || rc=1; done
if [ -s "$DEFERRED" ]; then
  echo "FAIL: $(wc -l <"$DEFERRED" | tr -d ' ') deferred check(s) failed:" >&2
  sed 's/^/  /' "$DEFERRED" >&2
  rc=1
fi
[ "$rc" = 0 ] || fail "a check above failed"

if [ "$MAPPER" = abc ]; then
  echo "PASS: sky130-shaped latch cells (polarity picks, clear/preset reset latches, dont_use/isolation/scan skipped), graph LEC proven, lgcheck not refuted"
  echo "PASS: ASAP7-shaped latch cells (QN low latch with D inverted, resets folded), graph LEC proven, lgcheck not refuted"
else
  echo "PASS: sky130-shaped latch cells via the USYN tmap (one cell per bit, clear/preset reset latches, dont_use/isolation/scan skipped, no USYN endpoint), graph LEC proven, lgcheck not refuted"
  echo "PASS: ASAP7-shaped latch cells via the USYN tmap (one cell per bit, resets folded, no dearer low latch), graph LEC proven, lgcheck not refuted"
fi
echo "PASS: fallback (a library without latch cells) keeps the latches native with the precise reason (graph LEC and lgcheck prove)"
echo "PASS: a wrong enable polarity / dropped clear (cvc5) and a swapped D / dropped reset / ungated enable (lgcheck) all refute"
echo "PASS: behind the fallback's inverter-cell enable, a wrong polarity and a swapped D refute (cvc5)"
