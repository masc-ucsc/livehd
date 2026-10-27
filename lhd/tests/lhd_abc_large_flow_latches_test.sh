#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc.large_flow_latches: a region at or above pass.abc.large_ge is mapped by the size tier's
# flow. The tool's own default large_flow only strashes and maps, so by default (the option is true)
# the region keeps every latch like the built-in flow and registers on a derived/gated clock or with an
# asynchronous reset map to DFF cells; the graph-native LEC proves the netlist against the source.
# With the option false the tier's flow counts as a user command list that may reshape latches and those
# registers stay native flops, which physical synthesis cannot place. A user-supplied large_flow keeps
# the old rule. large_ge=1 puts this small fixture in the tier.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/abc_derived_clock.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_abc_large_flow_latches_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

synth() { # <tag> [extra --set ...]
  local d="$W/$1"
  shift
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top abc_derived_clock --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.mapper=abc --set synth.liberty="$LIB" --set pass.abc.derived_clock=true \
    --set pass.abc.large_ge=1 "$@" -- "$SRC" > "$d/synth.log" 2>&1 || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
}

# Option false: the size tier's flow is a user command list; the gated, derived-clock and async-reset registers stay native.
synth off --set pass.abc.large_flow_latches=false
[ "$(count "$W/off/net.v" '^always @')" -ge 1 ] || fail "off: expected native flops in a size-tier region"
[ "$(count "$W/off/net.v" '^DFF')" -lt 12 ] || fail "off: every register was mapped although the tier's flow may reshape latches"
grep -q '"derived-clock-native"' "$W/off/synth.log" || fail "off: the derived-clock-native diagnostic is missing"

# Default (option true): the same region maps all 12 register bits to DFF cells.
synth on
[ "$(count "$W/on/net.v" '^always @')" = 0 ] || fail "on: a register stayed native: $(grep '^always @' "$W/on/net.v" | head -3)"
[ "$(count "$W/on/net.v" '^DFF')" = 12 ] || fail "on: expected 12 DFF cells, got $(count "$W/on/net.v" '^DFF')"
grep -q '"derived-clock-native"' "$W/on/synth.log" && fail "on: the registers were still reported native"

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/on/net" --ref lg:"$W/on/W/synth/lg" --lib lg:"$W/models" --top abc_derived_clock \
  --workdir "$W/lec" -q --result-json "$W/lec.json" > "$W/lec.log" 2>&1 || fail "on: LEC -> $(cat "$W/lec.json" 2>/dev/null)"
python3 - "$W/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK

# A user-supplied large_flow keeps the old rule even with the option.
synth user --set pass.abc.large_flow_latches=true --set "pass.abc.large_flow=strash; &get -n; &nf {D}; &put -o"
[ "$(count "$W/user/net.v" '^always @')" -ge 1 ] || fail "user: a user large_flow must keep the native-flop rule"

# Asynchronous-reset registers in a size-tier region (ASAP7-shaped DFFASRNx1: clear "!SETN" + preset "!RESETN"): by default
# they map onto the Liberty's clear/preset flop cells like in a normal region; with the option false they stay native.
ARST="$PWD/lhd/tests/abc_arst_mix.v"
QN="$PWD/lhd/tests/abc_arst_qn.lib"
arst() { # <tag> [extra --set ...]
  local d="$W/$1"
  shift
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top abc_arst_mix --workdir "$d/W" --emit-dir verilog:"$d/netv" \
    --set synth.mapper=abc --set synth.liberty="$QN" --set pass.abc.large_ge=1 "$@" -- "$ARST" > "$d/synth.log" 2>&1 \
    || fail "$d: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
}
arst arst_on
[ "$(count "$W/arst_on/net.v" '^always @')" = 0 ] || fail "arst_on: an async-reset register stayed native: $(grep '^always @' "$W/arst_on/net.v" | head -3)"
[ "$(count "$W/arst_on/net.v" '^DFFASRNx1 ')" = 15 ] || fail "arst_on: expected 15 DFFASRNx1, got $(grep -h '^DFF' "$W/arst_on/net.v" | sort | uniq -c)"
grep -q '"reset-native"' "$W/arst_on/synth.log" && fail "arst_on: the async-reset registers were reported native"
arst arst_off --set pass.abc.large_flow_latches=false
[ "$(count "$W/arst_off/net.v" '^always @')" -ge 1 ] || fail "arst_off: expected native async-reset flops with the option false"
grep -q '"reset-native"' "$W/arst_off/synth.log" || fail "arst_off: the reset-native diagnostic is missing"

echo "PASS: by default a region mapped by the size tier's default flow keeps its latches (derived-clock and async-reset registers map to cells)"
