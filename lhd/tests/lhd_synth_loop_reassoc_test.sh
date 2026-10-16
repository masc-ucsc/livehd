#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Loop accumulators reach synthesis as chains: a carried loop is expanded into
# one two-input cell per iteration, colored, so cprop's flattening skips them.
# For pass.abc, pass/synth/reassoc.cpp merges each chain the expansion minted
# into one n-ary cell per region and the blaster lowers it balanced; pass.usyn
# keeps the chains (its XAG flow measured neutral to worse on them). Checks:
#   - the source keeps its compact loop (loops stay loops in the graph),
#   - abc: the private copy reassociates the three 16-term chains and the
#     mapped delay beats the same flow without reassociation
#     (LIVEHD_SYNTH_REASSOC=0 is the debug switch that skips it),
#   - usyn: no reassociation,
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
synth() {  # synth <mapper> <mode> [env...]
  local mapper=$1 mode=$2
  shift 2
  env LIVEHD_SYNTH_REASSOC_DEBUG=1 "$@" "$LHD" synth lg:"$W/src" --top "$TOP" --set synth.liberty="$LIB" \
    --set synth.mapper="$mapper" --set lhd.jobs=1 --emit-dir lg:"$W/net_${mapper}_$mode" \
    --workdir "$W/synth_${mapper}_$mode" --result-json "$W/synth_${mapper}_$mode.json" >"$W/synth_${mapper}_$mode.log" 2>&1 ||
    { cat "$W/synth_${mapper}_$mode.log"; exit 1; }
}
for mapper in abc usyn; do
  synth "$mapper" on
  if [ "$mapper" = abc ]; then
    synth abc off LIVEHD_SYNTH_REASSOC=0
    # 3 chains x 15 links (the first iteration ORs/XORs into a constant, which
    # cprop folds); the debug line lands in the mapper's step log and is
    # printed only when reassociation runs.
    grep -rhEq 'reassoc: [0-9]+ guarded link\(s\); 3 associative tree\(s\) absorbed 4[0-9] link\(s\)' "$W/synth_abc_on/logs" ||
      { echo "FAIL: abc: chains not reassociated: $(grep -rh reassoc "$W/synth_abc_on/logs")" >&2; exit 1; }
    ! grep -rq 'reassoc:' "$W/synth_abc_off/logs"
    on=$(delay "$W/synth_abc_on.json")
    off=$(delay "$W/synth_abc_off.json")
    python3 -c "import sys; on, off = float('$on'), float('$off'); sys.exit(0 if on < off else 1)" ||
      { echo "FAIL: abc: reassociated delay $on is not below the chained $off" >&2; exit 1; }
    echo "abc: delay $off -> $on"
  else
    ! grep -rq 'reassoc:' "$W/synth_usyn_on/logs" || { echo "FAIL: usyn reassociated" >&2; exit 1; }
  fi
  run lec --set formal.effort=proof --impl lg:"$W/net_${mapper}_on" --ref lg:"$W/src" --lib lg:"$W/models" \
    --top "$TOP" --workdir "$W/lec_$mapper" --result-json "$W/lec_$mapper.json"
  grep -q '"verdict":"proven-' "$W/lec_$mapper.json" ||
    { echo "FAIL: $mapper: mapped netlist not PROVEN: $(cat "$W/lec_$mapper.json")" >&2; exit 1; }
done
echo "PASS: loop accumulator chains reassociate for abc, map shallower, and prove (abc + usyn)"
