#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A dynamic word select (`tbl[op]` over a packed word array, dino's ALU result
# select) lowers to `tbl >> (op*W + bias)`. Its right shift must map as a word
# mux over the narrow index, with satopt on AND off:
#   * off: the wide `(op << 5) + 32` Sum in the amount is itself a ware family;
#     enclosed before the shift it hid the affine amount (generic barrel);
#   * on: satopt's odc proves the amount's top value unobservable and narrows
#     it with a Get_mask(amount, 0x1ff) -- a wrapping link the affine
#     recognizer did not accept (dino: 4x the area of the word mux).
# So both runs must select the affine lowering, satopt-on must not map larger
# than satopt-off, and both netlists must stay equivalent to their reference.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
SRC=lhd/tests/satopt_word_select.sv
LIB=inou/prp/tests/abc/test.lib

for s in on off; do
  sat=true
  [ "$s" = off ] && sat=false
  "$LHD" synth --reader slang "$SRC" --top word_select --workdir "$W/w$s" --emit-dir "lg:$W/net$s" \
    --result-json "$W/r$s.json" -q --set synth.mapper=abc --set "synth.liberty=$LIB" --set synth.opentimer=false \
    --set "pass.satopt=$sat" --set pass.abc.verbose=true > "$W/log$s.txt" 2>&1 &
done
wait
for s in on off; do
  grep -q '"status":"pass"' "$W/r$s.json" || { cat "$W/log$s.txt"; echo "FAIL: synth satopt=$s"; exit 1; }
  grep -rq "affine right shift selected" "$W/log$s.txt" "$W/w$s/logs" \
    || { echo "FAIL: satopt=$s: the word select did not map as an affine word mux"; exit 1; }
done

python3 - "$W/ron.json" "$W/roff.json" <<'PY'
import json, sys
on, off = (json.load(open(p))['qor']['abc']['total']['area'] for p in sys.argv[1:3])
print(f'area satopt-on {on:.3f} satopt-off {off:.3f}')
assert on <= off * 1.001, f'satopt-on maps larger than satopt-off: {on} > {off}'
PY

"$LHD" pass liberty gensim "$LIB" --emit-dir "lg:$W/models" -q > /dev/null
for s in on off; do
  "$LHD" lec --impl "lg:$W/net$s" --ref "lg:$W/w$s/synth/lg" --lib "lg:$W/models" --top word_select.word_select \
    --workdir "$W/lec$s" --result-json "$W/lec$s.json" -q > "$W/lec$s.txt" 2>&1 \
    || { cat "$W/lec$s.txt"; echo "FAIL: LEC satopt=$s"; exit 1; }
done
echo PASS
