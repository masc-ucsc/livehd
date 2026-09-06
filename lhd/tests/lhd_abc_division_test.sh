#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_abc_division_$$}"
mkdir -p "$W"
cat > "$W/division.prp" <<'PRP'
comb division(a:u5, b:u3, c:s5, d:s3) -> (u:u5, s:s6, mixed:s7, narrow:u1, positive:u6) {
  u = a / b
  s = c / d
  mixed = c / b
  narrow = a / 17
  positive = (-a - 1) / (-b - 1)
}
PRP
LIB=inou/prp/tests/abc/test.lib
run() { "$LHD" "$@" -q --result-json "$W/r.json"; }
run compile "$W/division.prp" --emit-dir lg:"$W/ref" --workdir "$W/compile"
run synth lg:"$W/ref" --top division --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/net" --workdir "$W/synth"
run tool cat lg:"$W/net" --top division > "$W/net.txt"
# Guard the ABSENCE check: an empty dump (or a `tool cat` that stopped emitting
# nodes) would otherwise "prove" the divider is gone.
[ -s "$W/net.txt" ] || { echo 'FAIL: tool cat produced no netlist dump' >&2; exit 1; }
grep -q '"kind":' "$W/net.txt" || { echo 'FAIL: netlist dump has no node kinds -- the absence check is vacuous' >&2; exit 1; }
if grep -q '"kind":"div"' "$W/net.txt"; then echo 'native divider survived mapping' >&2; exit 1; fi
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/gensim"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/net" --ref lg:"$W/ref" --lib lg:"$W/models" --top division \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine"
  grep -q '"verdict":"proven"' "$W/r.json"
done
