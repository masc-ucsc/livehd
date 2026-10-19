#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.abc.large_flow_latches: a region at or above pass.abc.large_ge is mapped by the size tier's
# flow, which by default counts as a user command list that may reshape latches -- so registers on a
# derived/gated clock stay native flops, which physical synthesis cannot place. The tool's own default
# large_flow only strashes and maps, so with the option the region keeps every latch like the built-in
# flow and the registers map to DFF cells; the graph-native LEC proves the netlist against the source.
# A user-supplied large_flow keeps the old rule. large_ge=1 puts this small fixture in the tier.
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

# Default: the size tier's flow is a user command list; the gated, derived-clock and async-reset registers stay native.
synth off
[ "$(count "$W/off/net.v" '^always @')" -ge 1 ] || fail "off: expected native flops in a size-tier region"
[ "$(count "$W/off/net.v" '^DFF')" -lt 12 ] || fail "off: every register was mapped although the tier's flow may reshape latches"
grep -q '"derived-clock-native"' "$W/off/synth.log" || fail "off: the derived-clock-native diagnostic is missing"

# On: the same region maps all 12 register bits to DFF cells.
synth on --set pass.abc.large_flow_latches=true
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

echo "PASS: pass.abc.large_flow_latches keeps the latches of a region mapped by the size tier's default flow"
