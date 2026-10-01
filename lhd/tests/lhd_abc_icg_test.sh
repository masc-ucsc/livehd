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
#
# MAPPER=usyn (native USYN, default tmap=abc): a recognized clock gate is a
# native ICG barrier (clock-control state, never a USYN endpoint), so no ICG
# cell appears; with these ICG-cell libraries (no data-latch cell) its latch
# stays an always_latch and the `clk & latch` gating is ordinary mapped logic.
# (A Liberty with data-latch cells lets tmap map that latch onto one; see
# lhd_abc_latch_test.sh's gated fixture.) Every other claim holds:
# the same proofs, broken twins and reset-cell picks, all 12 source register
# bits kept by name as mapped DFF cells (none native), exactly the 4 gate
# latches native, and the schema-5 report's 12 register bits / 10 endpoints
# (the negedge `f` bits are not endpoints) for every library. The ABC-only
# claims (ICG cell picks and pins, pass.abc icg-native / derived-clock-native
# reason text) run only for MAPPER=abc; abc_icg_n instead asserts that its
# register stays 4 native negedge flops beside the native latch.
set -u

MAPPER="${MAPPER:-abc}"
ICG_CHECK="${ICG_CHECK:-both}"
case "$ICG_CHECK" in
  graph | verilog | both) ;;
  *) echo "FAIL: invalid ICG_CHECK=$ICG_CHECK" >&2; exit 1 ;;
esac
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

# usyn_field <synth dir> <python-expr over d>: one field of the schema-5 native
# decision report (`<qor>.usyn.json`, written for any tmap).
usyn_field() {
  local rep="$1/W/synth/qor.json.usyn.json"
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$rep" "$2" \
    || fail "unreadable USYN report $rep"
}

# usyn_state <synth dir> <tag> <register bits> <endpoints> <endpoint names>:
# the report's register/endpoint totals and endpoint names (the ICG latches are
# never endpoints), independent of the Liberty.
usyn_state() {
  local d="$1" tag="$2" v
  v="$(usyn_field "$d" 'd["schema_version"],d["kind"],d["output"],d["totals"]["register_bits"],d["totals"]["eligible_endpoints"]')"
  [ "$v" = "(5, 'usyn', 'mapped-cmos', $3, $4)" ] || fail "$tag: USYN report (schema,kind,output,register_bits,endpoints) = $v"
  v="$(usyn_field "$d" '" ".join(sorted(e["name"] for r in d["regions"] for e in r["endpoints"]))')"
  [ "$v" = "$5" ] || fail "$tag: USYN endpoints '$v', expected '$5'"
}

# The fixture's 12 register bits and the 4 gate latches (hierarchical names).
REG_BITS=('a[0]' 'a[1]' 'b[0]' 'b[1]' 'c[0]' 'c[1]' 'e[0]' 'e[1]' 'f[0]' 'f[1]' 'h[0]' 'h[1]')
GATE_LATCHES=(u0.en_latch u1.en_l u2.en_l u3.q)

# re <text>: <text> as a literal basic regular expression.
re() { printf '%s' "$1" | sed 's/[][\.*^$]/\\&/g'; }

# latch_enable <net.v> <latch>: the enable net of a native `always_latch`.
latch_enable() {
  grep -A1 '^always_latch' "$1" | sed -n "s/^ *if (\([^)]*\)) \\\\\{0,1\}$(re "$2")  *<=.*/\1/p"
}

# usyn_native_gates <tag> <synth dir>: the MAPPER=usyn form of "no native state
# survives": each gate latch is a native ICG barrier (always_latch, no ICG
# cell), and every source register bit is a mapped cell under its source name.
usyn_native_gates() {
  local tag="$1" d="$2" N="$2/net.v" l r
  ! grep -q "always @" "$N" || fail "$tag: a register stayed a native flop: $(grep 'always @' "$N" | head -3)"
  [ "$(count "$N" '^always_latch')" = 4 ] || fail "$tag: expected exactly the 4 gate latches native, got $(count "$N" '^always_latch')"
  for l in "${GATE_LATCHES[@]}"; do
    [ -n "$(latch_enable "$N" "$l")" ] || fail "$tag: gate latch $l is not a native always_latch"
  done
  ! grep -q "^S*DLCLK[A-Za-z0-9]* \|^ICG[A-Za-z0-9]* " "$N" || fail "$tag: an ICG cell replaced a native ICG barrier"
  for r in "${REG_BITS[@]}"; do
    [ "$(grep -c "^DFF[A-Za-z0-9]* \\\\$(re "$r") (" "$N")" = 1 ] || fail "$tag: register bit $r is not one mapped DFF cell"
  done
  grep -q '"native_state_nodes":4' "$d/r.json" || fail "$tag: the STA must see exactly the 4 native gate latches"
  usyn_state "$d" "$tag" 12 10 "a[0] a[1] b[0] b[1] c[0] c[1] e[0] e[1] h[0] h[1]"
}

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

# Each negative-control case needs the mapped netlist/models, but the positive
# case owns the original-vs-mapped proof and independent Verilog check.
prepare() {
  local d="$1" lib="$2" top="$3"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top "$top" --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.mapper="$MAPPER" --set synth.liberty="$lib" -- "$SRC" > "$d/synth.log" 2>&1 \
    || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
  "$LHD" pass liberty gensim "$lib" --emit-dir lg:"$d/models" --emit verilog:"$d/models.v" --workdir "$d/Wm" -q \
    --result-json "$d/g.json" > "$d/gensim.log" 2>&1 || fail "$d: gensim -> $(cat "$d/g.json")"
  { cat "$d/net.v"; echo; cat "$d/models.v"; } > "$d/impl.v"
  { cat "$SRC"; echo; cat "$d/models.v"; } > "$d/ref.v"
}

verify() {
  local d="$1" top="$2"
  if [ "$ICG_CHECK" != verilog ]; then
    "$LHD" lec --impl lg:"$d/net" --ref lg:"$d/W/synth/lg" --lib lg:"$d/models" --top "$top" --workdir "$d/Wlec" -q \
      --result-json "$d/lec.json" > "$d/lec.log" 2>&1
    grep -q '"verdict":"proven"' "$d/lec.json" || fail "$d: graph LEC did not prove: $(cat "$d/lec.json")"
  fi
  if [ "$ICG_CHECK" != graph ]; then
    local v
    v="$(lgcheck "$d/impl.v" "$d/ref.v" "$top" "$d/lgc")"
    case "$v" in
      proven | inconclusive) ;;
      *) fail "$d: lgcheck on the Verilog netlist: $v ($(tail -3 "$d/lgc/lgcheck.log"))" ;;
    esac
  fi
}

synth() {
  prepare "$1" "$2" "$3"
  verify "$1" "$3"
}

run_lib() {
  local tag="$1" d="$W/$1"
  synth "$d" "$PWD/lhd/tests/abc_icg_$tag.lib" "$TOP"
  ! grep -q '"derived-clock-native"' "$d/synth.log" \
    || fail "$tag: a gated register stayed native: $(grep -o '"derived-clock-native[^}]*' "$d/synth.log" | head -1)"
  if [ "$MAPPER" = abc ]; then
    ! grep -q "always @\|always_latch" "$d/net.v" || fail "$tag: native state survived: $(grep 'always @\|always_latch' "$d/net.v")"
    grep -q '"native_state_nodes":0' "$d/r.json" || fail "$tag: the STA still sees native state"
  else
    usyn_native_gates "$tag" "$d"
  fi
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

# Library/oracle cases are independent Bazel tests so each proof fits the
# debug runtime budget. Synthesis and proof process trees stay serial. Manual
# grouped cases reuse their prepared library across negative controls.
case_q() {
  run_lib q || exit 1
  N="$W/q/net.v"
  if [ "$MAPPER" = abc ]; then
    [ "$(count "$N" '^DLCLKPx1 ')" = 4 ] || fail "q: expected 4 DLCLKPx1 (one per gate), got $(grep -h '^[A-Z]*CLK' "$N")"
  fi
  ! grep -q "^DLCLKPx0 \|^DLCLKNx1 \|^DLCLKOx1 \|^SDLCLKPx1 " "$N" || fail "q: a dont_use / negedge / OR / dearer ICG was used"
  [ "$(count "$N" '^DFFRx1 ')" = 1 ] || fail "q: the reset-zero bit must map to a clear cell"
  [ "$(count "$N" '^DFFSx1 ')" = 1 ] || fail "q: the reset-one bit must map to a preset cell"
  # The chained gate's clock is the first gate's output, not the region clock.
  if [ "$MAPPER" = abc ]; then
    grep -A3 "^DLCLKPx1 .u2.en_l__icg" "$N" | grep -q "\.CLK(.u0.en_latch__icg" || fail "q: u2's cell must be clocked by u0's cell"
    echo "PASS: sky130-shaped ICG cells, chained gate, $ICG_CHECK checks"
  else
    # Native barriers: the enable expressions are the latch modules' own port
    # names, so this is only a structural shape check (u0/u1/u3 read the same
    # enable port, the chained u2 a different one). That u2 is really clocked by
    # u0's gated clock is established by this case's equivalence proof
    # ($ICG_CHECK: graph LEC and/or lgcheck against the source).
    local e0 e1 e2 e3
    e0="$(latch_enable "$N" u0.en_latch)" e1="$(latch_enable "$N" u1.en_l)"
    e2="$(latch_enable "$N" u2.en_l)" e3="$(latch_enable "$N" u3.q)"
    [ "$e0" = "$e1" ] && [ "$e3" = "$e1" ] || fail "q: u0/u1/u3 latch enables differ: $e0 $e1 $e3"
    [ "$e2" != "$e1" ] || fail "q: the chained u2 latch reads the same enable port as the region-clock gates"
    echo "PASS: usyn native ICG barriers on sky130-shaped cells, chained-gate shape, $ICG_CHECK checks"
  fi
}

case_qn() {
  run_lib qn || exit 1
  N="$W/qn/net.v"
  if [ "$MAPPER" = abc ]; then
    [ "$(count "$N" '^ICGx1 ')" = 4 ] || fail "qn: expected 4 ICGx1, got $(grep -h '^ICG' "$N")"
    [ "$(grep -A4 '^ICGx1 ' "$N" | grep -c "\.SE(1'h0)")" = 4 ] || fail "qn: every ICG test pin must be tied inactive"
  fi
  [ "$(count "$N" '^DFFASRNx1 ')" = 2 ] || fail "qn: both async-reset bits on a gated clock must map to DFFASRNx1"
  if [ "$MAPPER" = abc ]; then
    echo "PASS: ASAP7-shaped ICG cells (SE tied 0), $ICG_CHECK checks"
  else
    echo "PASS: usyn native ICG barriers on ASAP7-shaped cells, $ICG_CHECK checks"
  fi
}

case_n() {
  synth "$W/n" "$PWD/lhd/tests/abc_icg_q.lib" abc_icg_n
  if [ "$MAPPER" = abc ]; then
    grep -q 'derived-clock-native.*an OR (`clk | ~latch`' "$W/n/synth.log" \
      || fail "n: the OR-flavour gate must stay native with its reason: $(grep -o '"derived-clock-native[^}]*' "$W/n/synth.log")"
  else
    # Not an ICG: the register keeps its 4 source bits as native negedge flops
    # on the derived clock (no negedge endpoint), beside the native latch.
    local N="$W/n/net.v" r
    [ "$(count "$N" '^always @(negedge ')" = 4 ] || fail "n: expected 4 native negedge flops: $(grep 'always @' "$N")"
    for r in 'r[0]' 'r[1]' 'r[2]' 'r[3]'; do
      grep -A1 '^always @(negedge ' "$N" | grep -q "^\\\\$(re "$r")  *<=" || fail "n: register bit $r is not a native flop"
    done
    [ "$(count "$N" '^always_latch')" = 1 ] && [ -n "$(latch_enable "$N" en_latch)" ] \
      || fail "n: the gate latch en_latch must stay native"
    ! grep -q "^S*DLCLK[A-Za-z0-9]* \|^ICG[A-Za-z0-9]* \|^DFF[A-Za-z0-9]* " "$N" || fail "n: the OR-flavour gate or its register was mapped to a cell"
    grep -q '"native_state_nodes":5' "$W/n/r.json" || fail "n: the STA must see the 4 native flop bits and the latch"
    usyn_state "$W/n" n 4 0 ""
  fi

  echo "PASS: OR-flavour clock gate stays native"
}

case_none() {
  synth "$W/none" "$PWD/lhd/tests/abc_arst_q.lib" "$TOP"
  if [ "$MAPPER" = abc ]; then
    grep -q '"icg-native".*4 latch-based clock gate(s) kept as a native latch.*no usable integrated clock-gate cell' \
      "$W/none/synth.log" || fail "none: the gates must stay native, naming the missing ICG cell"
  else
    # A missing ICG cell cannot change the classification: the same native
    # barriers, registers and endpoints as with an ICG library.
    usyn_native_gates none "$W/none"
  fi
  grep -q "always_latch" "$W/none/net.v" || fail "none: the gate latches must stay native"
  ! grep -q "DLCLK\|ICGx" "$W/none/net.v" || fail "none: an ICG cell appeared without one in the library"

  echo "PASS: library without ICG cells keeps the gates native"
}

case_q_negative() {
  prepare "$W/q" "$PWD/lhd/tests/abc_icg_q.lib" "$TOP"
  for what in ${1:-scan inv en}; do
    case "$what" in
      scan) neg q scan "s/en_latch <= en_i | scan;/en_latch <= en_i;/" cvc5 || exit 1 ;;
      inv) neg q inv "s/assign gclk = clk \& en_l;/assign gclk = clk \& ~en_l;/" lgcheck || exit 1 ;;
      en) neg q en "s/always_latch if (!clk) en_l = en;/always_latch if (!clk) en_l = 1'b1;/" lgcheck || exit 1 ;;
      *) fail "invalid q negative control: $what" ;;
    esac
  done
  echo "PASS: q negative controls refute"
}

case_qn_negative() {
  prepare "$W/qn" "$PWD/lhd/tests/abc_icg_qn.lib" "$TOP"
  for what in ${1:-enfn chain}; do
    case "$what" in
      enfn) neg qn enfn "s/.en_i(en0 \& en2)/.en_i(en0)/" cvc5 || exit 1 ;;
      chain) neg qn chain "s/abc_icg_latch u2(.clk(g0)/abc_icg_latch u2(.clk(clk)/" lgcheck || exit 1 ;;
      *) fail "invalid qn negative control: $what" ;;
    esac
  done
  echo "PASS: qn negative controls refute"
}

case "${ICG_CASE:-all}" in
  q) case_q ;;
  qn) case_qn ;;
  q_negative) case_q_negative ;;
  qn_negative) case_qn_negative ;;
  q_scan) case_q_negative scan ;;
  q_inv) case_q_negative inv ;;
  q_en) case_q_negative en ;;
  qn_enfn) case_qn_negative enfn ;;
  qn_chain) case_qn_negative chain ;;
  n) case_n ;;
  none) case_none ;;
  all)
    case_q || exit 1
    case_qn || exit 1
    case_q_negative || exit 1
    case_qn_negative || exit 1
    case_n || exit 1
    case_none || exit 1
    ;;
  *) fail "invalid ICG_CASE=${ICG_CASE}" ;;
esac
