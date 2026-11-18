#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.state_sweep's whole-design state proof (pass/synth flat_state), with synth.flat_sweep off (the default):
# two ring_node instances keep each other's valid bit at 0, so every register in the ring holds its reset value
# forever and nothing ever reads its memory; a third instance, driven from the top inputs, is live. The ring
# instances get a content-named copy (`ring_node__f<hash>`) whose registers are folded away (0 DFF cells) and whose
# memory is dropped, `x` keeps the original definition (9 DFF cells and the memory), and the graph-native LEC proves
# the netlist against the compiled source without a bound.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/synth_flat_state.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_flat_state_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" synth --result-json "$W/r.json" --reader slang --top synth_flat_state --workdir "$W/W" --emit-dir lg:"$W/net" \
  --emit-dir verilog:"$W/netv" --set synth.mapper=abc --set synth.liberty="$LIB" -- "$SRC" > "$W/synth.log" 2>&1 \
  || fail "synth -> $(cat "$W/r.json" 2>/dev/null)"
copy=$(ls "$W/netv" | grep '^ring_node__f[0-9a-f_]*\.v$' | head -1)
[ -n "$copy" ] || fail "no flat-state copy of ring_node: $(ls "$W/netv")"
[ "$(grep -c '^DFF' "$W/netv/$copy")" = 0 ] || fail "the ring copy $copy still holds DFF cells"
[ "$(grep -c '^DFF' "$W/netv/ring_node.v")" = 9 ] || fail "ring_node (instance x) should keep its 9 DFF cells"
grep -q 'cgen_memory' "$W/netv/$copy" && fail "the ring copy $copy still instantiates its memory"
grep -q 'cgen_memory' "$W/netv/ring_node.v" || fail "ring_node (instance x) lost its memory"

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/net" --ref lg:"$W/W/synth/lg" --lib lg:"$W/models" --top synth_flat_state \
  --workdir "$W/lec" -q --result-json "$W/lec.json" > "$W/lec.log" 2>&1 || fail "LEC -> $(cat "$W/lec.json" 2>/dev/null)"
python3 - "$W/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK
echo PASS
