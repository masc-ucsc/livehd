#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `lhd pass abc` (task 2a-abc): technology-map a colored
# combinational design to a standard-cell netlist of blackbox Sub cells, and
# prove it LEC-equivalent to the original logic. The companion
# `pass liberty gensim` supplies a behavioral model per cell so the LEC stays
# self-contained (no PDK Verilog).
#
#   prp -> lg
#   pass color synth          (the abc driver coloring)
#   pass abc   --emit-dir lg:net   (partition + ABC tech-map per region)
#   pass partition --emit-dir lg:re  (same module structure, original logic)
#   pass liberty gensim test.lib --emit-dir lg:models
#   cgen net + models -> impl.v ; cgen re -> ref.v
#   lhd lec --set formal.solver=lgyosys (impl vs ref): must be LEC-equivalent
#   negative control: a corrupted reference must FAIL the check
#
# Hermetic: uses a small vendored Liberty (inou/prp/tests/abc/test.lib), not the
# sky130 PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
TIMING_LIB=inou/prp/tests/abc/timing.lib
PRP=inou/prp/tests/pyrope/abc_comb.prp
TOP=abc_comb.abc_comb
W="${TEST_TMPDIR:-/tmp/lhd_abc_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# 1. compile the flat combinational design to an lg library
run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
# 2. color every node (synth boundaries = the abc driver)
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
# 3. ABC technology-map each colored region -> standard-cell netlist. Every
# completed color must produce one compact, flushed heartbeat with a monotonic
# completion count; long synthesis wrappers rely on this stable prefix.
"$LHD" pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set synth.liberty="$LIB" --workdir "$W/w3" \
    --diag-fmt pretty --result-json "$W/r.json" >"$W/abc_stdout.log" 2>"$W/abc_progress.log" \
  || fail "pass abc -> $(cat "$W/r.json" 2>/dev/null)"
awk '
  /^PROGRESS pass[.]abc / {
    ++seen
    completed = 0
    for (i = 1; i <= NF; ++i) {
      if ($i ~ /^completed=/) {
        split($i, a, "=")
        completed = a[2] + 0
      }
    }
    if (completed != seen || $0 !~ / color=[-0-9]+ resynth=[01] cache=[^ ]+ ge=[0-9]+ gates=[0-9]+ ms=[0-9.]+/) {
      bad = 1
    }
  }
  END { exit seen == 0 || bad }
' "$W/abc_progress.log" || fail "pass abc completion heartbeat missing or inconsistent: $(cat "$W/abc_progress.log")"
# Verilog-only emission must trigger mapping without an explicit lg: output.
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir verilog:"$W/directv" \
    --set synth.liberty="$LIB" --workdir "$W/w_directv"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$W/directv/"*.v || fail "Verilog-only ABC emit did not map"
run pass abc --top "$TOP" lg:"$W/lg" --emit verilog:"$W/direct.v" \
    --set synth.liberty="$LIB" --workdir "$W/w_direct"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$W/direct.v" || fail "single-file ABC emit did not map"
for kind in pyrope ln lnast-dump sim isabelle lean; do
  if "$LHD" pass abc lg:"$W/lg" --emit-dir "$kind:$W/rejected_$kind" \
      --set synth.liberty="$LIB" -q --result-json "$W/rejected_$kind.json" 2>/dev/null; then
    fail "ABC silently accepted unsupported $kind output"
  fi
  # Per-kind json: a shared file would let iteration N match iteration N-1's
  # diagnostic when the failing invocation never rewrote it.
  [ -f "$W/rejected_$kind.json" ] || fail "unsupported $kind output produced no result json"
  grep -q '"class":"usage"' "$W/rejected_$kind.json" || fail "unsupported $kind output lacked a usage diagnostic"
  [ ! -e "$W/rejected_$kind" ] || fail "unsupported output was touched before rejection"
done

# 4. partition the SAME regions, keeping the original logic (the LEC twin)
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
# 5. behavioral model per combinational cell so the netlist Subs resolve for LEC
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

# 6. emit Verilog: impl = netlist modules + cell models ; ref = original logic
run compile lg:"$W/net" --top "$TOP" --emit-dir verilog:"$W/netv" --workdir "$W/w6"
run compile lg:"$W/models" --emit-dir verilog:"$W/modelsv" --workdir "$W/w7"
run compile lg:"$W/re" --top "$TOP" --emit-dir verilog:"$W/rev" --workdir "$W/w8"

# Direct pass emission and a separate compile of the mapped library agree.
for f in "$W/netv/"*.v; do
  cmp -s "$f" "$W/directv/$(basename "$f")" || fail "direct Verilog output differs from the mapped library"
done

# the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$W/netv/"*.v || fail "no standard cells in the ABC netlist"

cat "$W/netv/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"

# 7. LEC: the tech-mapped netlist must equal the original logic
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc"

# 8. negative control: a corrupted reference MUST fail the equivalence check
sed 's/\^/\&/g' "$W/ref.v" > "$W/ref_bad.v"
if "$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref_bad.v" --top "$TOP" \
    --workdir "$W/wcn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "negative control passed LEC against a corrupted reference (the check is not sound)"
fi

echo "PASS: pass.abc tech-map LEC-equivalent to original logic (+ negative control)"

# A delay target must switch the reported QoR from read_lib's unit-delay logic
# depth to the SCL timer's physical picoseconds (the Liberty's 2-D NLDM
# tables): untimed, the delay is a small integer; timed, it is a real delay.
# The mapper itself stays on the unit-delay GENLIB (the gain-100 GENLIB it
# used to derive for a delay target is gone: measured 1.63x yosys's area on
# ASAP7 against 1.24x without it), and the timed run exercises the SCL sizing
# tail with two NAND drive strengths -- ABC must be able to find every cell
# that dnsize/upsize consider -- plus the budget ladder's area candidate
# (qor.json says which mapping each region kept).
T="$W/timing"
mkdir -p "$T"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$T/unit" --set synth.liberty="$TIMING_LIB" \
    --set abc.max_fanout=0 --set abc.verbose=true --workdir "$T/w_unit"
unit_delay=$(grep -o '"max_delay":[0-9.]*' "$W/r.json" | head -1 | cut -d: -f2)
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$T/timed" --set synth.liberty="$TIMING_LIB" \
    --set abc.delay=1000 --set abc.verbose=true --workdir "$T/w_timed"
timed_delay=$(grep -o '"max_delay":[0-9.]*' "$W/r.json" | head -1 | cut -d: -f2)
grep -q 'resolved flow: .*&synch2 -K 6 -C 500' "$T/w_timed/logs/"*_lhd_pass_abc.log \
  || fail "delay did not select the flow2/K6 timing default"
grep -q 'resolved flow: .*&fraig -x; &put; dc2;' "$T/w_unit/logs/"*_lhd_pass_abc.log \
  || fail "untimed mapping did not retain the area baseline"
grep 'resolved flow:' "$T/w_timed/logs/"*_lhd_pass_abc.log | grep -q '&scorr' \
  && fail "the built-in timing flow must not perform sequential correlation"
awk -v unit="$unit_delay" -v timed="$timed_delay" 'BEGIN { exit !(unit > 0 && unit < 10 && timed > 10) }' \
  || fail "delay target did not activate NLDM timing (unit=$unit_delay timed=$timed_delay)"
# Gate depth stays a count even when delay changes to physical picoseconds.
python3 - "$T/w_unit/qor.json" "$T/w_timed/qor.json" <<'PY' || fail "invalid mapped logic depth"
import json
import sys
for path in sys.argv[1:]:
    q = json.load(open(path))
    depths = [r["logic_depth"] for r in q["regions"] if r["instances"] > 0]
    assert depths and all(type(d) is int and d > 0 for d in depths), q
    assert q["total"]["max_region_depth"] == max(depths), q
PY
grep -q "Derived GENLIB" "$T/w_timed/logs/"*_lhd_pass_abc.log \
  && fail "timed mapping re-derived a gain GENLIB: the mapper must stay on read_lib's unit-delay GENLIB"
# The objective ran: a 1000 ps budget (no flops here, so no register margin)
# and a decided candidate on the one region, with both SCL pairs recorded.
grep -q '"budget":1000.0' "$T/w_timed/qor.json" || fail "timed qor.json carries no 1000 ps region budget"
grep -q '"candidate":"\(area\|delay\)"' "$T/w_timed/qor.json" || fail "timed qor.json records no area/delay candidate decision"
grep -q '"delay_flow":{"delay":[0-9.]*,"area":[0-9.]*}' "$T/w_timed/qor.json" || fail "timed qor.json lacks the delay-flow SCL pair"
grep -q '"area_flow":{"delay":[0-9.]*,"area":[0-9.]*}' "$T/w_timed/qor.json" || fail "timed qor.json lacks the area-flow SCL pair"
grep -q '"budget"' "$T/w_unit/qor.json" && fail "untimed qor.json must not carry a budget"
run compile lg:"$T/timed" --top "$TOP" --emit-dir verilog:"$T/netv" --workdir "$T/w_emit"
run pass liberty gensim "$TIMING_LIB" --emit-dir lg:"$T/models" --workdir "$T/w_models"
run compile lg:"$T/models" --emit-dir verilog:"$T/modelsv" --workdir "$T/w_modelsv"
cat "$T/netv/"*.v "$T/modelsv/"*.v > "$T/impl.v"
run lec --set formal.solver=lgyosys --impl verilog:"$T/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$T/w_lec"
echo "PASS: pass.abc delay target uses physical NLDM delays (unit=$unit_delay ps, timed=$timed_delay ps)"

# Custom area flows own the untimed command list; an explicit flow still wins.
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$T/custom_area" --set synth.liberty="$LIB" \
    --set 'abc.area_flow=echo CUSTOM_AREA_SELECTED; strash; dc2; map' --workdir "$T/w_custom_area"
grep -q 'CUSTOM_AREA_SELECTED' "$T/w_custom_area/logs/"*_lhd_pass_abc.log || fail "untimed custom area_flow was ignored"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$T/custom_flow" --set synth.liberty="$LIB" \
    --set 'abc.area_flow=echo CUSTOM_AREA_SELECTED; strash; dc2; map' \
    --set 'abc.flow=echo CUSTOM_FLOW_SELECTED; strash; dc2; map' --workdir "$T/w_custom_flow"
grep -q 'CUSTOM_FLOW_SELECTED' "$T/w_custom_flow/logs/"*_lhd_pass_abc.log || fail "explicit flow was ignored"
grep -q 'CUSTOM_AREA_SELECTED' "$T/w_custom_flow/logs/"*_lhd_pass_abc.log && fail "area_flow overrode explicit flow"

# ---------------------------------------------------------------------------
# No prior coloring: `pass abc` must run WITHOUT `pass color` first. Color 0 (an
# uncolored design) is treated as just another color — the whole design folds
# into one color-0 region — with a single non-fatal warning, and the tech-mapped
# netlist stays LEC-equivalent to the original logic.
# ---------------------------------------------------------------------------
N="$W/nocolor"
mkdir -p "$N"
run compile "$PRP" --top "$TOP" --emit-dir lg:"$N/lg" --workdir "$N/w1"
# abc directly on the uncolored design (NO pass color) — must succeed + warn once
"$LHD" pass abc --top "$TOP" lg:"$N/lg" --emit-dir lg:"$N/net" --set synth.liberty="$LIB" \
    -q --result-json "$N/r.json" --workdir "$N/w2" || fail "pass abc without color failed -> $(cat "$N/r.json" 2>/dev/null)"
grep -q '"diagnostics_count":{"errors":0,"warnings":1}' "$N/r.json" \
  || fail "expected one uncolored-node warning, got $(grep -o '"diagnostics_count":{[^}]*}' "$N/r.json")"
# behavioral cell models + original-design reference, then emit + LEC
run pass liberty gensim "$LIB" --emit-dir lg:"$N/models" --workdir "$N/w3"
run compile lg:"$N/net" --top "$TOP" --emit-dir verilog:"$N/netv" --workdir "$N/w4"
run compile lg:"$N/models" --emit-dir verilog:"$N/modelsv" --workdir "$N/w5"
run compile lg:"$N/lg" --top "$TOP" --emit-dir verilog:"$N/origv" --workdir "$N/w6"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$N/netv/"*.v || fail "no standard cells in the uncolored ABC netlist"
cat "$N/netv/"*.v "$N/modelsv/"*.v > "$N/impl.v"
cat "$N/origv/"*.v > "$N/orig.v"
run lec --set formal.solver=lgyosys --impl verilog:"$N/impl.v" --ref verilog:"$N/orig.v" --top "$TOP" --workdir "$N/c"
echo "PASS: pass.abc runs WITHOUT a prior color pass (color-0 region, LEC-equivalent)"

# ---------------------------------------------------------------------------
# abc.rc script alias in `flow`: the library entry never sources abc.rc, so the
# pass installs the standard scripts (resyn2, compress2rs, ...) as aliases. A
# `flow="...resyn2..."` must resolve and still produce a LEC-equivalent netlist.
# Reuses the colored lg + cell models + reference from the top of this test.
# ---------------------------------------------------------------------------
A="$W/alias"
mkdir -p "$A"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$A/net" --set synth.liberty="$LIB" \
    --set abc.flow="strash; resyn2; &get -n; &dch -f; &nf {D}; &put" --workdir "$A/w1"
run compile lg:"$A/net" --top "$TOP" --emit-dir verilog:"$A/netv" --workdir "$A/w2"
grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$A/netv/"*.v || fail "no standard cells in the resyn2-mapped netlist (alias did not resolve?)"
cat "$A/netv/"*.v "$W/modelsv/"*.v > "$A/impl.v"
run lec --set formal.solver=lgyosys --impl verilog:"$A/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$A/c"
echo "PASS: pass.abc resolves abc.rc script aliases in flow (resyn2, LEC-equivalent)"

# ---------------------------------------------------------------------------
# The default large-region tier omits unbounded &dch choice synthesis. Force
# this tiny fixture through the tier, prove that selection was logged, and LEC
# the directly mapped result. This is the Backend-wide-shift escape path: it
# must remain a mapping recipe change, never a semantic approximation.
# ---------------------------------------------------------------------------
G="$W/large"
mkdir -p "$G"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$G/net" --set synth.liberty="$LIB" \
    --set abc.large_ge=1 --set abc.verbose=true --workdir "$G/w1"
grep -q "large_flow selected" "$G/w1/logs/"*_lhd_pass_abc.log \
  || fail "large-region tier was not selected at large_ge=1"
run compile lg:"$G/net" --top "$TOP" --emit-dir verilog:"$G/netv" --workdir "$G/w2"
cat "$G/netv/"*.v "$W/modelsv/"*.v > "$G/impl.v"
run lec --set formal.solver=lgyosys --impl verilog:"$G/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$G/c"
echo "PASS: pass.abc large-region direct flow is selected and LEC-equivalent"

# ---------------------------------------------------------------------------
# Constant shifts, slices, concats and set-mask cells are zero-delay wiring.
# A wide pack/unpack must remain typed native wiring instead of expanding into
# thousands of fake Liberty buffers/inverters. This is the retained-netlist
# memory shape exercised by Backend's DelayReg and register-file glue.
# ---------------------------------------------------------------------------
P="$W/wide_wiring"
mkdir -p "$P"
cat >"$P/wide_wiring.prp" <<'EOF'
pub mod wide_wiring(value:u4096, tag:u8) -> (out_value:u4096@[0], out_tag:u8@[]) {
  const packed = (value << 8) | tag
  out_value = packed#[8..=4103]
  out_tag = packed#[0..=7]
}
EOF
run compile "$P/wide_wiring.prp" --top wide_wiring --emit-dir lg:"$P/lg" --workdir "$P/w1"
run pass color synth --top wide_wiring.wide_wiring lg:"$P/lg" --workdir "$P/w2"
"$LHD" pass abc --top wide_wiring.wide_wiring lg:"$P/lg" --emit-dir lg:"$P/net" --set synth.liberty="$LIB" \
    --stats -q --result-json "$P/r.json" --workdir "$P/w3" \
  || fail "pass abc rejected wide native wiring -> $(cat "$P/r.json" 2>/dev/null)"
if grep -Eq '"gates":[1-9][0-9]*' "$P/r.json"; then
  fail "wide zero-delay wiring expanded into Liberty gates: $(cat "$P/r.json")"
fi
run lec --impl lg:"$P/net" --ref lg:"$P/lg" --top wide_wiring.wide_wiring --workdir "$P/w4"
echo "PASS: wide constant pack/unpack remains exact native zero-delay wiring"

# ---------------------------------------------------------------------------
# A real combinational SCC cannot enter ABC's acyclic Boolean network. Keep the
# exact typed remainder native, map the acyclic cones around it, and report the
# partial mapping explicitly. This is the IssueQueueVlduVstu shape that used to
# fail after thousands of Backend colors had already completed.
# ---------------------------------------------------------------------------
C="$W/comb_loop"
mkdir -p "$C"
run compile inou/prp/tests/pyrope/abc_comb_loop.prp --top abc_comb_loop --emit-dir lg:"$C/lg" --workdir "$C/w1"
run pass color synth --top abc_comb_loop.abc_comb_loop lg:"$C/lg" --workdir "$C/w2"
"$LHD" pass abc --top abc_comb_loop.abc_comb_loop lg:"$C/lg" --emit-dir lg:"$C/net" --set synth.liberty="$LIB" \
    --diag-fmt jsonl --result-json "$C/r.json" --workdir "$C/w3" 2>"$C/diag.jsonl" \
  || fail "pass abc rejected a preserved combinational SCC -> $(cat "$C/r.json" 2>/dev/null)"
grep -q '"code":"comb-loop-native"' "$C/diag.jsonl" \
  || fail "pass abc did not report the native combinational SCC boundary"
run compile lg:"$C/net" --top abc_comb_loop.abc_comb_loop --emit-dir verilog:"$C/netv" --workdir "$C/w4"
grep -q ' = (a & ' "$C/netv/"*.v || fail "mapped output dropped the native feedback expression"
"$LHD" pass opentimer --top abc_comb_loop.abc_comb_loop lg:"$C/net" "$LIB" --workdir "$C/w5" \
    --diag-fmt jsonl --result-json "$C/rt.json" 2>"$C/ot.jsonl" \
  || fail "opentimer rejected the explicit native SCC boundary -> $(cat "$C/rt.json" 2>/dev/null)"
grep -q '"code":"native-comb-boundary"' "$C/ot.jsonl" \
  || fail "opentimer did not report its partial native-combinational timing boundary"
grep -q '"kind":"sta"' "$C/w5/timing.json" || fail "native-boundary timing report missing"
echo "PASS: pass.abc preserves combinational SCCs and opentimer reports their explicit timing cuts"

# ---------------------------------------------------------------------------
# Feed-through wires must not become buffer cells. ABC materializes a Liberty
# buffer for every CI->CO edge and -- with the built-in flow's `&put -o` -- for
# every extra CO a gate drives (Abc_NtkLogicMakeSimpleCos, run by `&put` and by
# Abc_NtkToNetlist); the read-back aliases those away instead of minting a Sub.
# Before that: 512 of br_demux_onehot's 528 cells were such buffers (95% of its
# area), and this very fixture mapped to 10 cells (8 BUFx1 + a duplicated XOR).
# state=din2 (PI->latch D), out2=state (latch Q->PO) and out4 sharing out3's XOR
# (one gate -> 2 POs) are pure wiring: the netlist must carry NO buffer cell,
# exactly the one real gate (gates == the logic-only count), and stay
# LEC-equivalent to its partition twin.
# ---------------------------------------------------------------------------
FT="$W/feedthrough"
mkdir -p "$FT"
cat >"$FT/abc_feedthrough.prp" <<'EOF'
pub mod abc_feedthrough(clk:u1, din:u4, din2:u4, a:u1, b:u1) -> (out:u4@[0], out2:u4@[1], out3:u1@[0], out4:u1@[0]) {
  reg state:u4:[clock_pin=ref clk] = nil
  state = din2
  out = din
  out2 = state
  out3 = a ^ b
  out4 = a ^ b
}
EOF
run compile "$FT/abc_feedthrough.prp" --top abc_feedthrough --emit-dir lg:"$FT/lg" --workdir "$FT/w1"
run pass color synth --top abc_feedthrough.abc_feedthrough lg:"$FT/lg" --workdir "$FT/w2"
run pass partition --top abc_feedthrough.abc_feedthrough lg:"$FT/lg" --emit-dir lg:"$FT/re" --workdir "$FT/w3"
"$LHD" pass abc --top abc_feedthrough.abc_feedthrough lg:"$FT/lg" --emit-dir lg:"$FT/net" --set synth.liberty="$LIB" \
    -q --result-json "$FT/r.json" --workdir "$FT/w4" \
  || fail "pass abc on the feed-through design -> $(cat "$FT/r.json" 2>/dev/null)"
ft_total=$(grep -o '"total":{[^}]*}' "$FT/r.json" | head -1)
echo "$ft_total" | grep -q '"gates":1,' || fail "feed-through design must map to exactly one real gate: $ft_total"
echo "$ft_total" | grep -q '"bypassed":9' || fail "expected 9 bypassed identity buffers (8 CI->CO + 1 gate->2nd CO): $ft_total"
run compile lg:"$FT/net" --top abc_feedthrough.abc_feedthrough --emit-dir verilog:"$FT/netv" --workdir "$FT/w5"
! grep -hq "^BUFx1 " "$FT/netv/"*.v || fail "a feed-through wire became a BUFx1 buffer cell"
ft_cells=$(grep -hc "^\(NAND2x1\|NOR2x1\|INVx1\|XOR2x1\|BUFx1\) " "$FT/netv/"*.v | tr -d ' ')
[ "$ft_cells" = "1" ] || fail "feed-through netlist must hold exactly one comb cell (gates == logic-only count), got $ft_cells"
grep -hq "^DFFx1 " "$FT/netv/"*.v || fail "the resetless register did not map to DFF cells"
grep -hq "out2 = ({state_3\|out2 = {state_3" "$FT/netv/"*.v || fail "flop Q -> output is not a direct wire in the netlist"
run pass liberty gensim "$LIB" --emit-dir lg:"$FT/models" --workdir "$FT/w6"
run lec --impl lg:"$FT/net" --ref lg:"$FT/re" --lib lg:"$FT/models" --top abc_feedthrough.abc_feedthrough \
    --set formal.solver=cvc5 --workdir "$FT/w7"
echo "PASS: feed-through wires map to no buffer cell (identity-buffer bypass) and the netlist stays LEC-equivalent"

# Reduce-OR survives lowering in loop predicates and bit selections. Test
# both a wide unsigned input and a signed input, including its sign bit.
RO="$W/reduce_or"
mkdir -p "$RO"
cat >"$RO/reduce_or.prp" <<'PRP'
comb reduce_or(a:u65, b:s9) -> (y:u3) {
  y = a#|[..] + 2 * b#|[0..=8]
}
PRP
run compile "$RO/reduce_or.prp" --emit-dir lg:"$RO/lg" --workdir "$RO/w1"
run synth lg:"$RO/lg" --top reduce_or.reduce_or --emit-dir lg:"$RO/net" \
    --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$RO/w2"
run lec --impl lg:"$RO/net" --ref lg:"$RO/lg" --lib lg:"$W/models" --top reduce_or.reduce_or \
    --set formal.solver=cvc5 --workdir "$RO/w3"
echo "PASS: wide and signed reduce-OR map and remain equivalent"
