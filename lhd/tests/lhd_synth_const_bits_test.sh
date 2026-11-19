#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.state_sweep with synth.flat_sweep off (the default): constant bits cross instance boundaries
# (pass.specialize's per-bit join, run again on the mapper's copy after the state proof). `stage` instances register a flit whose upper 30 bits are 0, `thru` passes it
# on, `wmux` selects between two. With the propagation the design maps to fewer gates than with it disabled
# (LHD_NO_CONST_BITS), and the graph-native LEC proves the netlist against the compiled source without a bound.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/synth_const_bits.v"
LIB="$PWD/lhd/tests/abc_arst_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_synth_const_bits_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

synth() {  # synth <dir> [env ...]
  local d=$1
  shift
  env "$@" "$LHD" synth --result-json "$W/$d.json" --reader slang --top synth_const_bits --workdir "$W/$d" \
    --emit-dir lg:"$W/$d.net" --emit-dir verilog:"$W/$d.netv" --set synth.mapper=abc --set synth.liberty="$LIB" \
    -- "$SRC" > "$W/$d.log" 2>&1 || fail "synth $d -> $(cat "$W/$d.json" 2>/dev/null)"
}
synth on
synth off LHD_NO_CONST_BITS=1

python3 - "$W/on.json" "$W/off.json" <<'CHECK' || exit 1
import json, sys
def gates(path):
    total = json.load(open(path))['qor']['abc']['total']
    return total.get('gates', total.get('cells'))
on, off = gates(sys.argv[1]), gates(sys.argv[2])
if on is None or off is None or not on < off:
    raise SystemExit('FAIL: expected fewer gates with constant-bit propagation, got on={} off={}'.format(on, off))
print('gates on={} off={}'.format(on, off))
CHECK

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" -q --result-json "$W/g.json" > "$W/gensim.log" 2>&1 \
  || fail "gensim -> $(cat "$W/g.json")"
"$LHD" lec --set formal.effort=proof --impl lg:"$W/on.net" --ref lg:"$W/on/synth/lg" --lib lg:"$W/models" --top synth_const_bits \
  --workdir "$W/lec" -q --result-json "$W/lec.json" > "$W/lec.log" 2>&1 || fail "LEC -> $(cat "$W/lec.json" 2>/dev/null)"
python3 - "$W/lec.json" <<'CHECK' || exit 1
import json, sys
lec = json.load(open(sys.argv[1])).get('lec', {})
if lec.get('verdict') != 'proven-full' or lec.get('bounded'):
    raise SystemExit('FAIL: expected an unbounded LEC proof, got {}'.format(lec))
CHECK
echo PASS
