#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Loop accumulators reach synthesis as chains: a carried loop is expanded into
# one two-input cell per iteration, colored, so cprop's flattening skips them.
# For pass.abc, pass/synth/reassoc.cpp merges each chain the expansion minted
# into one n-ary cell per region and the blaster lowers it balanced; pass.usyn
# keeps the chains (its XAG flow measured neutral to worse on them). Checks:
#   - the source keeps its compact loop (loops stay loops in the graph),
#   - abc: the private copy reassociates the three 16-term chains (the
#     `--stats` loop line counts them) and the mapped delay beats the same
#     flow without reassociation (`--set pass.abc.reassoc=false`),
#   - usyn maps the chains as expanded (pass.usyn never asks for the merge),
#   - each mapped netlist is PROVEN against the source (formal.effort=proof).
set -euo pipefail
trap 'echo "FAIL at line $LINENO" >&2' ERR
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_synth_loop_reassoc_$$}"
SRC=lhd/tests/synth_loop_accum.prp
TOP=synth_loop_accum.synth_loop_accum
LIB=inou/prp/tests/abc/timing.lib
mkdir -p "$W"
run() { "$LHD" "$@" >"$W/latest.log" 2>&1 || { cat "$W/latest.log"; exit 1; }; }
run compile "$SRC" --emit-dir lg:"$W/src" --workdir "$W/compile"
grep -q '^has_loop_subnodes 1$' "$W/src/library.txt"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/gensim"
delay() {
  python3 - "$1" <<'PY'
import json, sys
q = json.load(open(sys.argv[1]))['qor']
print(min(d['max_delay'] for d in q['sta']['designs']))
PY
}
synth() {  # synth <mapper> <mode> [--set k=v...]
  local mapper=$1 mode=$2
  shift 2
  # The merge checks below are about pass/synth/reassoc.cpp, so the legs run the classic engine (whose loop cleanup leaves the
  # expanded chains for reassoc); the e-graph engine flattens them itself, see the default-engine leg after the loop.
  "$LHD" synth lg:"$W/src" --top "$TOP" --set synth.liberty="$LIB" --stats --set compile.cprop.engine=classic "$@" \
    --set synth.mapper="$mapper" --set lhd.jobs=1 --emit-dir lg:"$W/net_${mapper}_$mode" \
    --workdir "$W/synth_${mapper}_$mode" --result-json "$W/synth_${mapper}_$mode.json" >"$W/synth_${mapper}_$mode.log" 2>&1 ||
    { cat "$W/synth_${mapper}_$mode.log"; exit 1; }
}
for mapper in abc usyn; do
  synth "$mapper" on
  if [ "$mapper" = abc ]; then
    synth abc off --set pass.abc.reassoc=false
    # 3 chains x 15 links (the first iteration ORs/XORs into a constant, which
    # cprop folds); the `--stats` loop line lands in the mapper's step log.
    grep -rhEq 'reassoc: guarded=[0-9]+ trees=3 absorbed=4[0-9]$' "$W/synth_abc_on/logs" ||
      { echo "FAIL: abc: chains not reassociated: $(grep -rh 'reassoc:' "$W/synth_abc_on/logs")" >&2; exit 1; }
    grep -rhq 'reassoc: guarded=0 trees=0 absorbed=0$' "$W/synth_abc_off/logs" ||
      { echo "FAIL: abc: pass.abc.reassoc=false still merged: $(grep -rh 'reassoc:' "$W/synth_abc_off/logs")" >&2; exit 1; }
    on=$(delay "$W/synth_abc_on.json")
    off=$(delay "$W/synth_abc_off.json")
    python3 -c "import sys; on, off = float('$on'), float('$off'); sys.exit(0 if on < off else 1)" ||
      { echo "FAIL: abc: reassociated delay $on is not below the chained $off" >&2; exit 1; }
    echo "abc: delay $off -> $on"
  fi
  run lec --set formal.effort=proof --impl lg:"$W/net_${mapper}_on" --ref lg:"$W/src" --lib lg:"$W/models" \
    --top "$TOP" --workdir "$W/lec_$mapper" --result-json "$W/lec_$mapper.json"
  grep -q '"verdict":"proven-' "$W/lec_$mapper.json" ||
    { echo "FAIL: $mapper: mapped netlist not PROVEN: $(cat "$W/lec_$mapper.json")" >&2; exit 1; }
done

# The DEFAULT engine (e-graph): its flatten rule merges the same-colored expanded chains itself, so the mapped delay must be
# at least as good as the classic engine plus reassoc, with reassoc finding nothing left to merge, and the netlist proves.
"$LHD" synth lg:"$W/src" --top "$TOP" --set synth.liberty="$LIB" --stats --set synth.mapper=abc --set lhd.jobs=1 \
  --emit-dir lg:"$W/net_default" --workdir "$W/synth_default" --result-json "$W/synth_default.json" >"$W/synth_default.log" 2>&1 ||
  { cat "$W/synth_default.log"; exit 1; }
grep -rhq 'reassoc: guarded=0 trees=0 absorbed=0$' "$W/synth_default/logs" ||
  { echo "FAIL: default engine: reassoc still had chains to merge: $(grep -rh 'reassoc:' "$W/synth_default/logs")" >&2; exit 1; }
dflt=$(delay "$W/synth_default.json")
on=$(delay "$W/synth_abc_on.json")
python3 -c "import sys; d, on = float('$dflt'), float('$on'); sys.exit(0 if d <= on * 1.02 else 1)" ||
  { echo "FAIL: default engine delay $dflt is worse than classic + reassoc $on" >&2; exit 1; }
run lec --set formal.effort=proof --impl lg:"$W/net_default" --ref lg:"$W/src" --lib lg:"$W/models" \
  --top "$TOP" --workdir "$W/lec_default" --result-json "$W/lec_default.json"
grep -q '"verdict":"proven-' "$W/lec_default.json" ||
  { echo "FAIL: default engine: mapped netlist not PROVEN: $(cat "$W/lec_default.json")" >&2; exit 1; }
echo "default engine: delay $dflt (classic + reassoc $on)"
echo "PASS: loop accumulator chains reassociate for abc, map shallower, and prove (abc + usyn); the default engine merges them itself"
