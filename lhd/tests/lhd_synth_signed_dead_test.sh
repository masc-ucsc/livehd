#!/usr/bin/env bash
# The state sweep narrows SIGNED registers whose low bits nothing observes (synth_signed_dead.v: a signed tap chain
# that only reads each tap's top four bits). Before, a signed flop was only removed when every bit was dead, and a
# Sext/Mult demanded every operand bit, so all 32 tap bits stayed. Expect 4 DFF cells per tap (16 in all) and an unbounded LEC proof.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/synth_signed_dead.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_signed_dead_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" synth --result-json "$W/r.json" --reader slang --top synth_signed_dead --workdir "$W/W" --emit-dir lg:"$W/net" \
  --emit-dir verilog:"$W/netv" --set synth.mapper=abc --set synth.liberty="$LIB" -- "$SRC" > "$W/synth.log" 2>&1 \
  || fail "synth -> $(cat "$W/r.json" 2>/dev/null)"
tap=$(ls "$W/netv" | grep '^signed_tap' | head -1)
[ -n "$tap" ] || fail "no signed_tap definition in $(ls "$W/netv")"
dffs=$(grep -c '^ *DFF' "$W/netv/$tap" || true)
[ "$dffs" = 4 ] || fail "$tap should keep only its 4 observed bits as DFF cells, got $dffs"

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/net" --ref lg:"$W/W/synth/lg" --lib lg:"$W/models" --top synth_signed_dead \
  --workdir "$W/lec" -q --result-json "$W/lec.json" > "$W/lec.log" 2>&1 || fail "LEC -> $(cat "$W/lec.json" 2>/dev/null)"
python3 - "$W/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK
echo PASS
