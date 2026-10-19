#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.state_sweep: registers nothing observes are deleted from the mapper's private
# copy, registers read in part keep only what is read (a prefix is narrowed, scattered
# bits become single-bit registers named after the source bits), across the instance
# hierarchy; the graph-native LEC proves the mapped netlist against the source. The
# default (off) maps every register as before. synth.mapper=usyn honors the option too.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/state_sweep.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_state_sweep_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

count() { grep -h "$2" "$1" | wc -l | tr -d ' '; }

synth() { # <tag> <state_sweep> [extra --set ...]
  local d="$W/$1" sweep="$2"
  shift 2
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top sweep --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.liberty="$LIB" --set synth.state_sweep="$sweep" "$@" \
    -- "$SRC" > "$d/synth.log" 2>&1 || fail "$1: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
}

lec() { # <tag>
  "$LHD" lec --set formal.effort=proof --impl lg:"$W/$1/net" --ref lg:"$W/$1/W/synth/lg" --lib lg:"$W/models" --top sweep \
    --workdir "$W/$1/lec" -q --result-json "$W/$1/lec.json" > "$W/$1/lec.log" 2>&1 || fail "$1: LEC -> $(cat "$W/$1/lec.json" 2>/dev/null)"
  python3 - "$W/$1/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK
}

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"

# Default: every register of the source is mapped (184 bits: 32+32+8 in the child, 16, 64, 32 here).
synth off false
OFF=$(count "$W/off/net.v" '^DFF')
[ "$OFF" = 184 ] || fail "off: expected the 184 register bits of the source, got $OFF"
grep -q '"state-swept"' "$W/off/synth.log" && fail "off: the sweep reported work although it is off"
lec off

# On: the dead counter and the child's `sink` register go, the child's pipe keeps the 14 bits the parent
# reads (low byte, nibble 11:8 and bit 24 through the shifting stages), status keeps 13, taps 8 and the
# shift register all 64 (reading its far end needs every stage).
synth on true
ON=$(count "$W/on/net.v" '^DFF')
[ "$ON" = 99 ] || fail "on: expected 99 register bits, got $ON"
grep -q '"state-swept"' "$W/on/synth.log" || fail "on: the sweep did not report what it removed"
for r in 'lonely' 'sink'; do
  ! grep -q "^DFF[A-Za-z0-9]* \\\\*[A-Za-z0-9_.]*$r" "$W/on/net.v" || fail "on: the unobserved register $r is still mapped"
done
grep -q '^DFF[A-Za-z0-9]* \\.*pipe\[24\] ' "$W/on/net.v" || fail "on: bit 24 of the child's pipe lost its name"
lec on

# The sweep removes the cells behind the registers too: the mapped netlist gets smaller.
CELLS='^[A-Z][A-Za-z0-9]*x[0-9][0-9]* '
[ "$(count "$W/on/net.v" "$CELLS")" -lt "$(count "$W/off/net.v" "$CELLS")" ] || fail "on: the swept netlist is not smaller"

# The native unate mapper shares the sweep: its logical netlist keeps the same registers.
usyn() { # <tag> <state_sweep>
  local d="$W/usyn_$1"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top sweep --workdir "$d/W" --emit verilog:"$d/net.v" \
    --set synth.mapper=usyn --set pass.usyn.tmap=none --set synth.state_sweep="$2" -- "$SRC" > "$d/synth.log" 2>&1 \
    || fail "usyn $1: synth -> $(cat "$d/r.json" 2>/dev/null)"
}
usyn off false
usyn on true
[ "$(count "$W/usyn_off/net.v" '^always @')" = 184 ] || fail "usyn off: expected 184 registers, got $(count "$W/usyn_off/net.v" '^always @')"
[ "$(count "$W/usyn_on/net.v" '^always @')" = 99 ] || fail "usyn on: expected 99 registers, got $(count "$W/usyn_on/net.v" '^always @')"

echo "PASS: synth.state_sweep drops unobserved registers and register bits, proven against the source"
