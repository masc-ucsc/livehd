#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q; }
run compile inou/prp/tests/equiv/loop_break_leftover.prp --emit-dir lg:"$W/source" --workdir "$W/c"
# Partition uses its existing private occurrence expansion; the source library
# remains compact. Recompilation must preserve every constant carry operand.
run pass partition lg:"$W/source" --top top --set pass.partition.flatten=true \
  --emit-dir lg:"$W/partitioned" --workdir "$W/p"
run compile lg:"$W/partitioned" --top top --emit-dir lg:"$W/cleaned" --emit verilog:"$W/cleaned.v" --workdir "$W/cleanup"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/cleaned" --ref pyrope:inou/prp/tests/equiv/loop_break_leftover_1.prp \
    --impl-top loop_break_leftover.top --ref-top top --set formal.solver="$engine" \
    --set formal.timeout=60 --workdir "$W/$engine"
done
# ABC must specialize its own private iteration bodies before mapping: the
# compact source stays intact while constant indices and break conditions fold.
LIB=inou/prp/tests/abc/test.lib
run synth lg:"$W/source" --top top --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/mapped" --workdir "$W/synth" --result-json "$W/synth.json"
python3 - "$W/synth.json" <<'PYQ'
import json,sys
q=json.load(open(sys.argv[1]))['qor']['abc']['total']; assert q['gates'] < 100,q
PYQ
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/models-work"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/mapped" --ref pyrope:inou/prp/tests/equiv/loop_break_leftover_1.prp \
    --lib lg:"$W/models" --impl-top loop_break_leftover.top --ref-top top --set formal.solver="$engine" \
    --set formal.timeout=60 --workdir "$W/mapped-$engine"
done
echo 'PASS: cleanup preserves constant carries after partitioning'
