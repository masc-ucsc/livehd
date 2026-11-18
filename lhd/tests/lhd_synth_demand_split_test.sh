#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.state_sweep with split_by_demand: two instances of one definition, the parent reading the data output
# of one and only the valid bit of the other. The unread instance gets a content-named copy
# (`demand_stage__d<hash>`) whose 16 data registers the sweep drops: 17 + 1 = 18 DFF cells instantiated instead
# of 2 x 17 = 34 (each definition is emitted once, so the module bodies hold exactly those 18). The read instance
# keeps the original definition. The graph-native LEC proves the netlist
# against the compiled source.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/synth_demand_split.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_demand_split_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" synth --result-json "$W/r.json" --reader slang --top synth_demand_split --workdir "$W/W" --emit-dir lg:"$W/net" \
  --emit-dir verilog:"$W/netv" --set synth.mapper=abc --set synth.liberty="$LIB" -- "$SRC" > "$W/synth.log" 2>&1 \
  || fail "synth -> $(cat "$W/r.json" 2>/dev/null)"
cat "$W/netv/"*.v > "$W/net.v"
ls "$W/netv" | grep -q '^demand_stage__d[0-9a-f]*\.v$' || fail "no demand-specialized copy of demand_stage: $(ls "$W/netv")"
dffs=$(grep -c '^DFF' "$W/net.v")
[ "$dffs" = 18 ] || fail "expected 18 DFF cells in the module bodies (17 + the copy's valid bit), got $dffs"

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/net" --ref lg:"$W/W/synth/lg" --lib lg:"$W/models" --top synth_demand_split \
  --workdir "$W/lec" -q --result-json "$W/lec.json" > "$W/lec.log" 2>&1 || fail "LEC -> $(cat "$W/lec.json" 2>/dev/null)"
python3 - "$W/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK
echo PASS
