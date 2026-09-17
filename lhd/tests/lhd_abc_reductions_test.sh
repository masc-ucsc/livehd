#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_abc_reductions_$$}"
mkdir -p "$W"
cat > "$W/reductions.prp" <<'PRP'
comb reductions(a:u9, b:s8) -> (parity:u1, population:u4, signed_parity:u1, signed_population:u4) {
  parity = a#^[..]
  population = a#+[..]
  signed_parity = b#^[1..=7]
  signed_population = b#+[1..=7]
}
PRP
run() { "$LHD" "$@" -q --result-json "$W/r.json"; }
run compile "$W/reductions.prp" --emit-dir lg:"$W/ref" --workdir "$W/compile"
run tool cat lg:"$W/ref" --top reductions > "$W/ref.txt"
grep -q '"kind":"rxor"' "$W/ref.txt"
grep -q '"kind":"popcount"' "$W/ref.txt"
LIB=inou/prp/tests/abc/test.lib
run synth lg:"$W/ref" --top reductions --set synth.liberty="$LIB" --set synth.opentimer=false \
  --set synth.threads=1 --emit-dir lg:"$W/net" --workdir "$W/synth"
run tool cat lg:"$W/net" --top reductions > "$W/net.txt"
grep -q '"kind":' "$W/net.txt"
if grep -Eq '"kind":"(rxor|popcount)"' "$W/net.txt"; then
  echo 'native reduction survived mapping' >&2
  exit 1
fi
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/gensim"
run lec --impl lg:"$W/net" --ref lg:"$W/ref" --lib lg:"$W/models" --top reductions \
  --set formal.timeout=60 --workdir "$W/lec"
grep -q '"verdict":"proven"' "$W/r.json"
