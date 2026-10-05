#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Partition-boundary environment for pass.abc (pass/abc/abc_boundary.cpp):
# a region is sized against what lies BEYOND its ports. The fixture maps one
# NAND into region 1 and its eight consumers into region 2. On its own, region
# 1 sees a load-free output and keeps NAND2x1; against the eight sink pins and
# region 2's departure it must take NAND2x2 (the hermetic timing.lib carries
# exactly those two drive strengths).
#
#   boundary=true  : qor.json reports crossing bits + a re-sized cell, the
#                    driver region holds NAND2x2, and the refined netlist is
#                    LEC-equivalent to the partition twin (cell swaps only)
#   boundary=false : no boundary scoreboard, NAND2x1 stays
#   incremental    : a second run is all hits, starts no ABC, and emits the
#                    byte-identical (refined) netlist -- the cache holds the
#                    refined bodies and the refinement is skipped
#
# The re-size runs in ROUNDS (`boundary_rounds`), and the two halves of the
# environment arrive at different rounds. The exact loads and drivers are there
# from round one; the arrival/required BUDGETS travel one region hop per round.
# This fixture's upsize is entirely the second kind -- what makes the NAND
# critical is the delay through region 2, which reaches region 1 only after the
# budget has walked back through the wrapper -- so it needs THREE rounds, and
# the runs that assert the upsize ask for them explicitly. The default is one
# round (case 1b pins what that alone does), so a run without $ROUNDS below is
# testing the default, not this contract.
#
# MAPPER=usyn: native USYN's optional ABC tmap maps the selected network under
# the USYN timing target `pass.usyn.delay` (abc.* options are not inherited) and
# applies the same PHYSICAL-only techniques under pass.usyn spellings: the
# boundary environment and exact re-size (`pass.usyn.boundary`,
# `boundary_rounds`), fanout buffering (`max_fanout`, `boundary_buffer`) and
# sizing to the budget. So the NAND2x1 -> NAND2x2 upsize and both switches hold
# for USYN too. The qor.json scoreboard and pass.abc's dont-use diagnostic are
# pass.abc report facts, gated on MAPPER=abc. USYN also keeps: a timing-only
# change remaps without invalidating native selection, a dont_use cell never
# maps, the mapped netlist is LEC-equivalent to the partition twin, and an
# all-hit re-run reuses both the native selection and the mapped bodies
# byte-identically.
set -u

# One script, both technology mappers: MAPPER=abc (default) runs `lhd pass abc`
# and MAPPER=usyn runs `lhd pass usyn`. lhd/tests/BUILD generates the `_usyn`
# twin from this same file; the pass.abc-only claims are gated (see above).
MAPPER="${MAPPER:-abc}"
case "$MAPPER" in
  abc | usyn) ;;
  *)
    echo "FAIL: bad MAPPER=$MAPPER (expected abc|usyn)" >&2
    exit 1
    ;;
esac

# The rounds the propagated-budget contract needs; NOT the default (see above).
# Both are word-split on purpose (no value holds a space).
if [ "$MAPPER" = abc ]; then
  ROUNDS="--set abc.boundary_rounds=3"
  DELAY="--set abc.delay=25"
else
  ROUNDS="--set pass.usyn.boundary_rounds=3"
  DELAY="--set pass.usyn.delay=25"
fi

LHD=lhd/lhd
LIB=inou/prp/tests/abc/timing.lib
PRP=inou/prp/tests/pyrope/abc_boundary.prp
TOP=abc_boundary.abc_boundary
W="${TEST_TMPDIR:-/tmp/lhd_abc_boundary_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() {
  if [ "${1:-}" = pass ] && [ "${2:-}" = abc ] && [ -n "${ABC_TEST_THREADS:-}" ]; then
    set -- "$@" --set "synth.threads=$ABC_TEST_THREADS"
  fi
  "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"
}

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"

# 1. boundary on: with the budget propagated back across the hierarchy, the
# driver region takes the stronger NAND
run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_on" --set synth.liberty="$LIB" \
    $DELAY $ROUNDS --workdir "$W/w_on"
if [ "$MAPPER" = abc ]; then
  python3 - "$W/w_on/qor.json" "$TOP" <<'PY' || fail "boundary=true qor.json has no boundary scoreboard"
import json, sys
q = json.load(open(sys.argv[1]))
b = q["total"]["boundary"]
assert b["bits"] > 0 and b["resized"] >= 1, b
c1 = [r for r in q["regions"] if r["module"] == sys.argv[2] + "__c1"][0]
assert c1["boundary"]["resized"] >= 1 and c1["boundary"]["delay_pre"] > 0, c1
PY
else
  # qor.json is the separate technology-map report (tmap=abc); the native
  # schema-5 decision report sits beside it. The USYN timing target is the
  # budget of every mapped region, and the crossing driver is timed under the
  # real library.
  python3 - "$W/w_on/qor.json" "$W/w_on/qor.json.usyn.json" "$TOP" <<'PY' \
    || fail "pass.usyn.delay=25 did not reach the technology-map regions"
import json, sys
q = json.load(open(sys.argv[1]))
n = json.load(open(sys.argv[2]))
assert q["kind"] == "technology-map" and q["provider"] == "abc", q
assert n["schema_version"] == 5 and n["tmap"] == "abc" and n["output"] == "mapped-cmos", n
assert q["regions"] and all(r["budget"] == 25.0 for r in q["regions"]), q["regions"]
c1 = [r for r in q["regions"] if r["module"] == sys.argv[3] + "__c1"][0]
assert c1["gates"] >= 1 and c1["delay"] > 0, c1
PY
fi
run compile lg:"$W/net_on" --top "$TOP" --emit-dir verilog:"$W/v_on" --workdir "$W/wv_on"
grep -q "NAND2x2" "$W/v_on/${TOP}__c1.v" || fail "boundary=true left the crossing driver on NAND2x1"
if [ "$MAPPER" = abc ]; then
  grep -q "pass.abc boundary: .* cell(s) re-sized" "$W/w_on/logs/"*_lhd_pass_${MAPPER}.log \
    || fail "no boundary refinement summary in the pass log"
fi

# 1b. the DEFAULT single round: the exact environment is still built (the
# scoreboard reports the same crossing bits, and the region is timed under the
# real loads -- delay_pre), but one round carries no budget across a region
# hop, so this fixture's upsize does not fire. Pinning it keeps the two halves
# of the mechanism apart: a future default that resizes here is a change worth
# noticing, not a silent improvement.
if [ "$MAPPER" = abc ]; then
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_1r" --set synth.liberty="$LIB" \
      --set abc.delay=25 --workdir "$W/w_1r"
  python3 - "$W/w_1r/qor.json" "$TOP" <<'PY' || fail "the default single round lost the boundary environment"
import json, sys
q = json.load(open(sys.argv[1]))
b = q["total"]["boundary"]
assert b["bits"] > 0 and b["rounds"] == 1, b
c1 = [r for r in q["regions"] if r["module"] == sys.argv[2] + "__c1"][0]
assert c1["boundary"]["delay_pre"] > 0, c1
PY
else
  # USYN analogue: drop the timing target on the SAME workdir. Every mapped
  # region loses its budget and is re-mapped, while native selection -- whose
  # cache key excludes tmap/timing settings -- is reused for every region.
  run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_1r" --set synth.liberty="$LIB" \
      --workdir "$W/w_on"
  python3 - "$W/r.json" "$W/w_on/qor.json.usyn.json" <<'PY' \
    || fail "a timing-only change must remap every region and reuse every native selection"
import json, sys
q = json.load(open(sys.argv[1]))["qor"]
n = json.load(open(sys.argv[2]))
assert q["kind"] == "technology-map" and len(q["regions"]) >= 2, q
assert all(r["budget"] <= 0 for r in q["regions"]), q["regions"]
inc = q["incremental"]
assert inc["enabled"] and inc["hits"] == 0 and inc["misses"] == len(q["regions"]), inc
assert all(r["cache"] == "miss" for r in q["regions"]), q["regions"]
c = n["cache"]
assert c["enabled"] and c["reused"] == len(n["regions"]) >= 2 and c["misses"] == 0 and c["invalid"] == 0, c
assert all(r["cache_reused"] for r in n["regions"]), n["regions"]
PY
fi

# 2. boundary off: the old behaviour, load-free ports (both mappers' switch)
PREFIX=$([ "$MAPPER" = abc ] && echo abc || echo pass.usyn)
run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_off" --set synth.liberty="$LIB" \
    $DELAY $ROUNDS --set "$PREFIX.boundary=false" --workdir "$W/w_off"
if [ "$MAPPER" = abc ]; then
  grep -q '"boundary"' "$W/w_off/qor.json" && fail "boundary=false still reports a boundary scoreboard"
fi
run compile lg:"$W/net_off" --top "$TOP" --emit-dir verilog:"$W/v_off" --workdir "$W/wv_off"
grep -q "NAND2x2" "$W/v_off/${TOP}__c1.v" && fail "boundary=false sized the crossing driver up"

# 2b. a primary input's fanout is tree-buffered: ABC's `buffer` only trees an
# input that has a driving cell, and pass.abc declares its stand-in as one
# (boundary_buffer, independent of `boundary`). 64 sinks on `en` need >= 4
# buffers under max_fanout=16; boundary_buffer=false leaves the port bare.
# The tree is required of the USYN tmap too (it maps under the same default
# fanout cap and exposes the same boundary_buffer switch).
PRP2=inou/prp/tests/pyrope/abc_pi_fanout.prp
TOP2=abc_pi_fanout.abc_pi_fanout
[ -f "$PRP2" ] || fail "missing fixture $PRP2"
run compile "$PRP2" --top "$TOP2" --emit-dir lg:"$W/lg2" --workdir "$W/w2"
run pass "$MAPPER" --top "$TOP2" lg:"$W/lg2" --emit-dir lg:"$W/net_fan" --set synth.liberty="$LIB" --workdir "$W/w_fan"
run compile lg:"$W/net_fan" --top "$TOP2" --emit-dir verilog:"$W/v_fan" --workdir "$W/wv_fan"
n=$(cat "$W/v_fan/"*.v | grep -cE '^\s*BUFx1\s')
[ "$n" -ge 4 ] || fail "primary input with fanout 64 got $n buffer(s), expected a tree of >= 4"
run pass "$MAPPER" --top "$TOP2" lg:"$W/lg2" --emit-dir lg:"$W/net_fan_off" --set synth.liberty="$LIB" \
    --set "$PREFIX.boundary_buffer=false" --workdir "$W/w_fan_off"
run compile lg:"$W/net_fan_off" --top "$TOP2" --emit-dir verilog:"$W/v_fan_off" --workdir "$W/wv_fan_off"
cat "$W/v_fan_off/"*.v | grep -qE '^\s*BUFx1\s' && fail "boundary_buffer=false still buffered the primary input"

# 2c. a Liberty cell marked dont_use never maps (both mappers), and pass.abc
# reports it ONCE per run (its own `dont-use` diagnostic; the USYN tmap
# provider has no such report)
python3 - "$LIB" "$W/du.lib" <<'PY' || fail "could not derive the dont_use library"
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r'cell\s*\(\s*BUFx1\s*\)\s*\{', text)
assert m, "no BUFx1 in the fixture library"
depth, i = 0, m.end() - 1
while True:
    depth += {'{': 1, '}': -1}.get(text[i], 0)
    if depth == 0:
        break
    i += 1
group = text[m.start():i + 1]
twin = group.replace('BUFx1', 'BUFx1_du', 1).replace('{', '{\n    dont_use : true;', 1)
close = text.rstrip().rfind('}')
open(sys.argv[2], 'w').write(text[:close] + twin + '\n' + text[close:])
PY
run pass "$MAPPER" --top "$TOP2" lg:"$W/lg2" --emit-dir lg:"$W/net_du" --set synth.liberty="$W/du.lib" \
    --emit diagnostics:"$W/du.jsonl" --workdir "$W/w_du"
if [ "$MAPPER" = abc ]; then
  [ "$(grep -c '"code":"dont-use"' "$W/du.jsonl")" = 1 ] \
    || fail "expected exactly one dont_use report, got: $(grep '"code":"dont-use"' "$W/du.jsonl" 2>/dev/null)"
  grep -q "ignored 1 Liberty cell(s) marked dont_use (e.g. BUFx1_du)" "$W/du.jsonl" \
    || fail "dont_use report does not name the cell: $(grep '"code":"dont-use"' "$W/du.jsonl")"
fi
run compile lg:"$W/net_du" --top "$TOP2" --emit-dir verilog:"$W/v_du" --workdir "$W/wv_du"
cat "$W/v_du/"*.v | grep -q "BUFx1_du" && fail "a dont_use cell was mapped"
# not vacuous: the buffer tree the dont_use twin would have served is still built
n=$(cat "$W/v_du/"*.v | grep -cE '^\s*BUFx1\s')
[ "$n" -ge 4 ] || fail "the dont_use library lost the primary-input buffer tree ($n BUFx1)"

# 3. the refined netlist is LEC-equivalent to the partition twin
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w_re"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w_models"
run compile lg:"$W/models" --emit-dir verilog:"$W/modelsv" --workdir "$W/w_modelsv"
run compile lg:"$W/re" --top "$TOP" --emit-dir verilog:"$W/rev" --workdir "$W/w_rev"
cat "$W/v_on/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"
run lec --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/w_lec"
echo "PASS: boundary-sized netlist is LEC-equivalent to the partition twin"

# 4. incremental: the cache holds the refined bodies; an all-hit run neither
# starts ABC nor re-refines, and emits the same netlist
I="$W/incr"
mkdir -p "$I"
run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$I/net1" --set synth.liberty="$LIB" \
    $DELAY $ROUNDS --workdir "$I/w"
run compile lg:"$I/net1" --top "$TOP" --emit-dir verilog:"$I/v1" --workdir "$I/wv1"
run pass "$MAPPER" --top "$TOP" lg:"$W/lg" --emit-dir lg:"$I/net2" --set synth.liberty="$LIB" \
    $DELAY $ROUNDS --workdir "$I/w"
if [ "$MAPPER" = abc ]; then
  grep -q '"incremental":{"hits":[1-9][0-9]*,"misses":0' "$I/w/qor.json" || fail "second run was not all hits: $(grep -o '"incremental":{[^}]*}' "$I/w/qor.json")"
  grep -q '"abc_started":0' "$I/w/qor.json" || fail "all-hit run started ABC"
  grep -q "pass.abc boundary:" "$I/w/logs/"*_lhd_pass_${MAPPER}.log && {
    n=$(grep -c "pass.abc boundary:" "$I/w/logs/"*_lhd_pass_${MAPPER}.log | awk -F: '{s+=$NF} END {print s}')
    [ "$n" = "1" ] || fail "the all-hit run re-ran the boundary refinement"
  }
else
  # Both USYN reuse tiers: every native region replays its cached selection
  # (no endpoint search), and every technology-map region restores its mapped
  # body (no ABC mapping: no row is a miss).
  python3 - "$W/r.json" "$I/w/qor.json.usyn.json" <<'PY' || fail "second USYN run was not all hits at both tiers"
import json, sys
q = json.load(open(sys.argv[1]))["qor"]
n = json.load(open(sys.argv[2]))
inc = q["incremental"]
assert inc["enabled"] and inc["misses"] == 0 and inc["hits"] == len(q["regions"]) >= 2, inc
assert not inc["invalid"] and not inc["store_failed"], inc
assert all(r["cache"] == "hit" for r in q["regions"]), q["regions"]
c = n["cache"]
assert c["enabled"] and c["misses"] == 0 and c["stored"] == 0 and c["reused"] == len(n["regions"]) >= 2, c
assert all(r["cache_reused"] for r in n["regions"]), n["regions"]
PY
fi
run compile lg:"$I/net2" --top "$TOP" --emit-dir verilog:"$I/v2" --workdir "$I/wv2"
for f in "$I/v1/"*.v; do
  cmp -s "$f" "$I/v2/$(basename "$f")" || fail "all-hit run emitted a different netlist: $(basename "$f")"
done
grep -q "NAND2x2" "$I/v2/${TOP}__c1.v" || fail "the cached body lost the boundary re-size"
echo "PASS: all-hit incremental run reuses the refined bodies without ABC"

if [ "$MAPPER" = abc ]; then
  echo "PASS: pass.abc partition-boundary environment"
else
  echo "PASS: pass.usyn timing target, boundary re-size/buffering, mapped-netlist LEC and two-tier reuse"
fi
