#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.state_sweep: registers nothing observes are deleted from the mapper's private
# copy, registers read in part keep only what is read (a prefix is narrowed, scattered
# bits become single-bit registers named after the source bits), across the instance
# hierarchy; the graph-native LEC proves the mapped netlist against the source. The
# default (off) maps every register as before. synth.mapper=usyn honors the option too.
# synth.merge_registers merges twin registers.
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

# synth.merge_registers: twin registers (same clock, enable, reset and D) merge, and the registers fed by
# them merge in turn; a register with another enable stays. 5 x 8 bits become 3 x 8. The twin of a merged
# register has no counterpart left in the netlist, so the LEC proof is a bounded one.
MERGE_SRC="$PWD/lhd/tests/reg_merge.v"
for mode in false true; do
  d="$W/merge_$mode"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top reg_merge --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.liberty="$LIB" --set synth.merge_registers="$mode" -- "$MERGE_SRC" > "$d/synth.log" 2>&1 \
    || fail "merge $mode: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
done
[ "$(count "$W/merge_false/net.v" '^DFF')" = 40 ] || fail "merge off: expected 40 register bits, got $(count "$W/merge_false/net.v" '^DFF')"
[ "$(count "$W/merge_true/net.v" '^DFF')" = 24 ] || fail "merge on: expected 24 register bits, got $(count "$W/merge_true/net.v" '^DFF')"
grep -q '"registers-merged"' "$W/merge_true/synth.log" || fail "merge on: the merge did not report what it did"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/merge_true/net" --ref lg:"$W/merge_true/W/synth/lg" --lib lg:"$W/models" \
  --top reg_merge --workdir "$W/merge_true/lec" -q --result-json "$W/merge_true/lec.json" > "$W/merge_true/lec.log" 2>&1 \
  || fail "merge on: LEC -> $(cat "$W/merge_true/lec.json" 2>/dev/null)"
python3 - "$W/merge_true/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') not in ('proven-full', 'proven-bounded') or (lec.get('bounded') and lec.get('bound', 0) < 4):
    raise SystemExit('FAIL: expected a LEC proof of the merged twins, got {}'.format(lec))
CHECK

# A lowered memory is registers behind a read mux: with the sweep, the columns no reader looks at go too
# (4 entries x 32 bits -> the 8 bits of the byte read). The removed columns have no counterpart left to pair, so
# the LEC proof is a bounded one.
MEM_SRC="$PWD/lhd/tests/state_sweep_mem.v"
for mode in false true; do
  d="$W/mem_$mode"
  mkdir -p "$d"
  "$LHD" synth --result-json "$d/r.json" --reader slang --top sweep_mem --workdir "$d/W" --emit-dir lg:"$d/net" \
    --emit-dir verilog:"$d/netv" --set synth.liberty="$LIB" --set synth.memory=flops --set synth.state_sweep="$mode" \
    -- "$MEM_SRC" > "$d/synth.log" 2>&1 || fail "mem $mode: synth -> $(cat "$d/r.json" 2>/dev/null)"
  cat "$d/netv/"*.v > "$d/net.v"
done
[ "$(count "$W/mem_false/net.v" '^DFF')" = 128 ] || fail "mem off: expected 128 register bits, got $(count "$W/mem_false/net.v" '^DFF')"
[ "$(count "$W/mem_true/net.v" '^DFF')" = 32 ] || fail "mem on: expected 32 register bits, got $(count "$W/mem_true/net.v" '^DFF')"
grep -q '"state-swept-memory"' "$W/mem_true/synth.log" || fail "mem on: the sweep of the lowered memory did not report"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/mem_true/net" --ref lg:"$W/mem_true/W/synth/lg" --lib lg:"$W/models" \
  --top sweep_mem --workdir "$W/mem_true/lec" -q --result-json "$W/mem_true/lec.json" > "$W/mem_true/lec.log" 2>&1 \
  || fail "mem on: LEC -> $(cat "$W/mem_true/lec.json" 2>/dev/null)"
python3 - "$W/mem_true/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') not in ('proven-full', 'proven-bounded') or (lec.get('bounded') and lec.get('bound', 0) < 4):
    raise SystemExit('FAIL: expected a LEC proof of the narrowed memory, got {}'.format(lec))
CHECK

echo "PASS: synth.state_sweep drops unobserved registers and register bits, proven against the source"
