#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Asynchronous-reset registers map onto the Liberty's clear/preset flop cells
# (pass/liberty/liberty_dff.cpp areset_ladder, pass/synth region_blast +
# region_writer) instead of staying native flops.
#
# Fixture lhd/tests/abc_arst_mix.v: an active-high (`posedge rst`, value 1010)
# and an active-low (`negedge rst_n`, value 0110) async register, one reset
# through an inverter inside the region (`~rst_n`, value 1111), a
# prim_rst_sync-shaped synchronizer whose resets are computed inside the region
# (a scan mux over a register: they cross ABC as outputs and mapped logic
# drives the cell pins), and a synchronous-reset register. Two hermetic libraries:
#   abc_arst_q.lib  (sky130-shaped): DFFRx1 clear "!RB", DFFSx1 preset "S"
#                   (active HIGH: the active-low reset needs an INVx1), a
#                   cheaper dont_use clear cell that must never appear, and a
#                   dearer dual cell that is never picked.
#   abc_arst_qn.lib (ASAP7-shaped): QN-only; DFFASRNx1 carries clear "!SETN" +
#                   preset "!RESETN", so a bit resetting to 0 drives SETN (QN
#                   forced 0), one resetting to 1 drives RESETN, and the other
#                   pin is tied high.
# For each: no native flop survives, the cell/pin choice is exactly the one
# above, the netlist is proven against the source graph by the graph-native
# LEC (cvc5, gensim models), the Verilog netlist + gensim Verilog models are
# proven by the independent yosys checker (lgyosys), and two broken twins
# refute: a flipped reset-value bit (cvc5 on graphs) and an async reset turned
# synchronous in the reference (lgyosys; the graph-native LEC abstracts one
# commit per step and cannot tell async from sync at P==1).
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
SRC=lhd/tests/abc_arst_mix.v
TOP=abc_arst_mix
W="${TEST_TMPDIR:-/tmp/lhd_abc_arst_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

# run_lib <tag>: synthesize, prove, and leave the netlist in $W/<tag>.
run_lib() {
  local tag="$1" lib="lhd/tests/abc_arst_$1.lib" d="$W/$1"
  mkdir -p "$d"
  local r="$d/r.json"
  run() { "$LHD" "$@" -q --result-json "$r" > "$d/last.log" 2>&1 || fail "$tag: $* -> $(cat "$r" 2>/dev/null)"; }
  # (everything after `--` goes to the reader, so the result options come first)
  "$LHD" synth -q --result-json "$r" --reader slang --top "$TOP" --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --emit diagnostics:"$d/diag.jsonl" --set synth.mapper="$MAPPER" \
    --set synth.liberty="$lib" -- "$SRC" > "$d/synth.log" 2>&1 || fail "$tag: synth -> $(cat "$r" 2>/dev/null)"
  ! grep -q '"reset-native"' "$d/diag.jsonl" \
    || fail "$tag: an async-reset register stayed native: $(grep '"reset-native"' "$d/diag.jsonl")"
  cat "$d/netv/"*.v > "$d/net.v"
  ! grep -q "always @" "$d/net.v" || fail "$tag: a native flop survived: $(grep 'always @' "$d/net.v")"
  run pass liberty gensim "$lib" --emit-dir lg:"$d/models" --emit verilog:"$d/models.v" --workdir "$d/Wm"
  run lec --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/models" --top "$TOP" --workdir "$d/Wlec"
  grep -q '"verdict":"proven"' "$r" || fail "$tag: graph LEC did not prove: $(cat "$r")"
  { cat "$d/net.v"; echo; cat "$d/models.v"; } > "$d/impl.v"
  { cat "$SRC"; echo; cat "$d/models.v"; } > "$d/ref.v"
  run lec --impl verilog:"$d/impl.v" --ref verilog:"$d/ref.v" --top "$TOP" --set formal.solver=lgyosys --workdir "$d/Wlgy"
  grep -q '"verdict":"proven"' "$r" || fail "$tag: lgyosys did not prove the Verilog netlist: $(cat "$r")"
}

# neg <tag> <name> <sed> <cvc5|lgyosys>: the reference with one reset turned
# wrong must refute against the <tag> netlist.
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
  else
    { cat "$d/src.v"; echo; cat "$W/$tag/models.v"; } > "$d/ref.v"
    "$LHD" lec --impl verilog:"$W/$tag/impl.v" --ref verilog:"$d/ref.v" --top "$TOP" --set formal.solver=lgyosys \
      --set formal.simfail_run=false --workdir "$d/Wlec" -q --result-json "$d/r.json" > "$d/lec.log" 2>&1
  fi
  grep -q '"verdict":"refuted"' "$d/r.json" || fail "$tag/$what: broken twin was not refuted ($solver): $(cat "$d/r.json")"
}

run_lib q &
qpid=$!
run_lib qn &
qnpid=$!
wait "$qpid" || exit 1
wait "$qnpid" || exit 1

# --- sky130-shaped: per-bit clear/preset choice and reset polarity ----------
N="$W/q/net.v"
[ "$(count "$N" '^DFFRx1 ')" = 6 ] || fail "q: expected 6 DFFRx1 (a, b, e bits resetting to 0 + rst_q), got $(grep -h '^DFF' "$N")"
[ "$(count "$N" '^DFFSx1 ')" = 9 ] || fail "q: expected 9 DFFSx1 (a, b, e bits resetting to 1 + all of c), got $(grep -h '^DFF' "$N")"
[ "$(count "$N" '^DFFx1 ')" = 4 ] || fail "q: expected 4 DFFx1 for the synchronous-reset register"
! grep -q "DFFRx0\|DFFRSx1" "$N" || fail "q: a dont_use or dearer dual cell was used"
for b in 0 2; do grep -A5 "^DFFRx1 a_$b(" "$N" | grep -q "RB(abc_reset_inv" || fail "q: a[$b] (resets to 0 on posedge rst) must take ~rst on RB"; done
for b in 1 3; do grep -A5 "^DFFSx1 a_$b(" "$N" | grep -q "\.S(rst)" || fail "q: a[$b] (resets to 1 on posedge rst) must take rst on S"; done
for b in 0 3; do grep -A5 "^DFFRx1 b_$b(" "$N" | grep -q "RB(rst_n)" || fail "q: b[$b] (resets to 0 on negedge rst_n) must take rst_n on RB"; done
for b in 1 2; do grep -A5 "^DFFSx1 b_$b(" "$N" | grep -q "\.S(abc_reset_inv" || fail "q: b[$b] (resets to 1 on negedge rst_n) must take ~rst_n on S"; done
for b in 0 1 2 3; do grep -A5 "^DFFSx1 c_$b(" "$N" | grep -q "\.S(abc_reset_inv" || fail "q: c[$b] (reset ~rst_n traced to rst_n) must take ~rst_n on S"; done
[ "$(count "$N" '^INVx1 abc_reset_inv')" = 2 ] || fail "q: expected one shared reset inverter per input (rst, rst_n)"
# The synchronizer's resets are computed in the region (a scan mux, a register):
# mapped logic, not a region input, drives those pins.
for r in "DFFRx1 rst_q" "DFFSx1 e_0" "DFFRx1 e_1"; do
  pin="$(grep -A5 "^$r(" "$N" | grep -o '\.\(RB\|S\)([^)]*)')"
  case "$pin" in
    *"(g"*) ;;
    *) fail "q: '$r' reset pin must be driven by mapped logic, got '$pin'" ;;
  esac
done
echo "PASS: sky130-shaped clear/preset cells per reset bit, polarity through shared inverters, dont_use honored, cvc5 + lgyosys proven"

# --- ASAP7-shaped: the dual QN cell, the other pin tied inactive -------------
N="$W/qn/net.v"
[ "$(count "$N" '^DFFASRNx1 ')" = 15 ] || fail "qn: expected 15 DFFASRNx1, got $(grep -h '^DFF' "$N")"
[ "$(count "$N" '^DFFNx1 ')" = 4 ] || fail "qn: expected 4 DFFNx1 for the synchronous-reset register"
grep -A6 "^DFFASRNx1 a_0(" "$N" | grep -q "SETN(abc_reset_inv" || fail "qn: a[0] resets to 0: SETN must take ~rst"
grep -A6 "^DFFASRNx1 a_0(" "$N" | grep -q "RESETN(1'h1)" || fail "qn: a[0]: RESETN must be tied inactive"
grep -A6 "^DFFASRNx1 a_1(" "$N" | grep -q "RESETN(abc_reset_inv" || fail "qn: a[1] resets to 1: RESETN must take ~rst"
grep -A6 "^DFFASRNx1 b_0(" "$N" | grep -q "SETN(rst_n)" || fail "qn: b[0] resets to 0 on negedge rst_n: SETN must take rst_n"
grep -A6 "^DFFASRNx1 c_0(" "$N" | grep -q "RESETN(rst_n)" || fail "qn: c[0] resets to 1 on ~rst_n: RESETN must take rst_n"
echo "PASS: ASAP7-shaped dual QN clear/preset cell (reset0=SETN, reset1=RESETN), cvc5 + lgyosys proven"

# --- broken twins must refute --------------------------------------------------
neg q val "s/if (rst) a <= 4'b1010;/if (rst) a <= 4'b1011;/" cvc5 &
p1=$!
neg qn val "s/if (!rst_n) b <= 4'b0110;/if (!rst_n) b <= 4'b0111;/" cvc5 &
p2=$!
neg qn sync "s/always @(posedge clk or posedge rst)/always @(posedge clk)/" lgyosys &
p3=$!
neg q synchro "s/if (!rst_sync_n) e <= 2'b01;/if (!rst_sync_n) e <= 2'b11;/" cvc5 &
p4=$!
wait "$p1" || exit 1
wait "$p2" || exit 1
wait "$p3" || exit 1
wait "$p4" || exit 1
echo "PASS: flipped async reset values (cvc5, incl. the synchronizer-reset register) and an async reset made synchronous (lgyosys) all refute"
