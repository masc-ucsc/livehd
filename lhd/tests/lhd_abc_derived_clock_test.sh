#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc.derived_clock: a register clocked by logic computed inside its own region
# (a `clk & latch` gate with no integrated clock-gate cell in the Liberty, or any other
# combinational derived clock) stays a native flop by default (derived-clock-native) --
# physical synthesis cannot place that. With the option it maps to a plain DFF / clear /
# preset cell whose clock pin the mapped logic drives, and the graph-native LEC proves
# the netlist against the source. A negedge register on a derived clock stays native.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/abc_derived_clock.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"  # DFF, clear and preset cells; no ICG cell
W="${TEST_TMPDIR:-/tmp/lhd_abc_derived_clock_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

synth() { # <tag> <top> <option>
  local d="$W/$1"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top "$2" --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.mapper=abc --set synth.liberty="$LIB" --set pass.abc.derived_clock="$3" \
    -- "$SRC" > "$d/synth.log" 2>&1 || fail "$1: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
}

# Default: the 3 registers (a, b, c: 12 bits) stay native on their derived clocks.
synth off abc_derived_clock false
grep -q '"derived-clock-native"' "$W/off/synth.log" || fail "off: the derived-clock-native diagnostic is missing"
[ "$(count "$W/off/net.v" '^always @')" -ge 1 ] || fail "off: expected native flops on the derived clocks"

# Option on: no native flop, 12 DFF cells (plain, clear, preset) named after the registers.
synth on abc_derived_clock true
N="$W/on/net.v"
[ "$(count "$N" '^always @')" = 0 ] || fail "on: a register stayed native: $(grep '^always @' "$N" | head -3)"
[ "$(count "$N" '^DFF')" = 12 ] || fail "on: expected 12 DFF cells, got $(count "$N" '^DFF')"
for r in 'a[0]' 'b[3]' 'c[0]'; do
  grep -q "^DFF[A-Za-z0-9]* \\\\$(printf '%s' "$r" | sed 's/[][\.*^$]/\\&/g') (" "$N" || fail "on: register bit $r is not a DFF cell"
done
# Every register bit's clock pin is a mapped-logic net, never the bare region clock.
! grep -A3 '^DFF' "$N" | grep -q '\.CLK(clk)' || fail "on: a derived-clock register was clocked by the raw clock"
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

# A negedge register on a derived clock stays native even with the option.
synth neg abc_derived_clock_neg true
[ "$(count "$W/neg/net.v" '^always @(negedge ')" = 1 ] || fail "neg: expected the native negedge flop: $(grep 'always @' "$W/neg/net.v")"
grep -q '"derived-clock-native"' "$W/neg/synth.log" || fail "neg: the negedge register must be reported native"

echo "PASS: pass.abc.derived_clock maps derived-clock registers to DFF cells"
