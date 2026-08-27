#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `pass.color synth --set synth_alg=cones`
# (todo/livehd/2c-color-synthcones.html): the cone-seeded coloring reaches
# pass.partition and pass.abc through the real CLI, and what comes out the far
# end still computes the same function.
#
#   prp -> lg (O1, hierarchy intact)
#   reference Verilog from the untouched library
#   lhd pass color synth --set color.synth_alg=cones --set color.max_gate=<small>
#   lhd pass partition   (one <def>__c<id> module per color id)
#   lhd pass abc         (tech-map every region)
#   lhd lec              (partitioned vs original): must be equivalent
#
# The unit tests in pass/color pin the WALK and the merge; this pins the
# plumbing a gtest cannot reach: the --set spelling, the recorded
# coloring_info (`"synth_alg":"cones"`, `"max_gate"`, and the "packed":true that
# keeps pass.partition from shredding a first-wins cone back into per-cloud
# modules), and the fact that partition and ABC accept the result.
#
# max_gate is deliberately tiny here: these fixtures are a few dozen predicted
# gates, so the shipped 30000 would merge everything into one color and the
# multi-region path -- the one that can go wrong -- would never run.
set -u
LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_color_cones_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# fixture   top
DESIGNS=(
  "hier_comb hier_comb.top"
  "hier_seq  hier_seq.top"
)

for entry in "${DESIGNS[@]}"; do
  set -- $entry
  FIX="$1"; TOP="$2"
  PRP="inou/prp/tests/pyrope/$FIX.prp"
  [ -f "$PRP" ] || fail "missing fixture $PRP"
  D="$W/$FIX"
  mkdir -p "$D"

  run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"

  run pass color synth --top "$TOP" --stats \
      --set color.synth_alg=cones --set color.max_gate=40 \
      lg:"$D/lg" --workdir "$D/w3"

  # The coloring descriptor must say what actually ran: downstream readers
  # (pass.partition's anchor union, pass.abc's admission hints, the incremental
  # cache) key off it. It lives as a JSON blob on the top graph's INPUT_NODE, so
  # it is read back out of the serialized body.
  info_has() { LC_ALL=C grep -raq -- "$1" "$D/lg"; }
  info_has '"synth_alg":"cones"' || fail "$FIX: coloring_info does not record synth_alg=cones"
  info_has '"max_gate":40' || fail "$FIX: coloring_info does not record max_gate"
  # A cones color is first-wins, so an earlier owner can split a later cone into
  # disjoint clouds. Without "packed" pass.partition emits one
  # `<def>__c<id>_r<k>` module per cloud, ordered by min-nid -- unstable cache
  # keys for pass.abc.
  info_has '"packed":true' || fail "$FIX: cones must record packed=true unconditionally"

  run pass partition --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/part" --workdir "$D/w4"
  run compile lg:"$D/part" --top "$TOP" --recipe O0 --emit verilog:"$D/post.v" --workdir "$D/w5"

  # One module per color id, and none of the per-cloud `_r<k>` splits that a
  # missing "packed" flag would produce.
  grep -qE "^module ${TOP//./_}__c[0-9]+ " "$D/post.v" \
    || grep -qE "__c[0-9]+" "$D/post.v" \
    || fail "$FIX: partition emitted no <def>__c<id> region module"
  grep -qE "__c[0-9]+_r[0-9]+" "$D/post.v" && fail "$FIX: packed color was split into per-cloud _r modules"

  run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/c"
  echo "PASS: $FIX cones -> partition -> LEC-equivalent"
done

# Phase 2, the forward merge across Q. Both modes must reach pass.partition and
# stay LEC-equivalent; `forward` is inert by default, so the runs above already
# covered the off case.
for MODE in pair all; do
  D="$W/fwd_$MODE"
  mkdir -p "$D"
  run compile "inou/prp/tests/pyrope/hier_seq.prp" --top hier_seq.top --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top hier_seq.top --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"
  run pass color synth --top hier_seq.top --set color.synth_alg=cones --set color.max_gate=40 \
      --set color.forward="$MODE" lg:"$D/lg" --workdir "$D/w3"
  LC_ALL=C grep -raq -- "\"forward\":\"$MODE\"" "$D/lg" || fail "forward=$MODE not recorded in coloring_info"
  run pass partition --top hier_seq.top lg:"$D/lg" --emit-dir lg:"$D/part" --workdir "$D/w4"
  run compile lg:"$D/part" --top hier_seq.top --recipe O0 --emit verilog:"$D/post.v" --workdir "$D/w5"
  run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top hier_seq.top --workdir "$D/c"
  echo "PASS: cones forward=$MODE -> partition -> LEC-equivalent"
done

# A typo must be refused, not silently treated as off.
if "$LHD" pass color synth --top hier_seq.top --set color.synth_alg=cones --set color.forward=maybe \
     lg:"$W/hier_seq/lg" --workdir "$W/negf" -q --result-json "$W/negf.json" 2>/dev/null; then
  fail "an unknown forward mode was accepted"
fi
echo "PASS: unknown forward mode is refused"

# max_gate=0 is RAW cones: no merge at all, so it must produce at least as many
# colors as a capped run. This is the contract Color_opts documents (0 = inert),
# and the one a user reaches for when debugging a partition.
D="$W/raw"
mkdir -p "$D"
run compile "inou/prp/tests/pyrope/hier_seq.prp" --top hier_seq.top --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
cp -R "$D/lg" "$D/lg_capped"
run pass color synth --top hier_seq.top --set color.synth_alg=cones --set color.max_gate=0 \
    lg:"$D/lg" --workdir "$D/w2"
run pass color synth --top hier_seq.top --set color.synth_alg=cones --set color.max_gate=1000000 \
    lg:"$D/lg_capped" --workdir "$D/w3"
count_colors() {
  "$LHD" tool --diag-fmt pretty cat --top hier_seq.top lg:"$1" 2>/dev/null | grep -o 'color=[0-9]*' | sort -u | wc -l | tr -d ' '
}
RAW=$(count_colors "$D/lg")
CAP=$(count_colors "$D/lg_capped")
# `RAW >= CAP` alone is an IDENTITY, not an observation: merging only ever
# coarsens and max_gate=0 short-circuits merge_colors entirely, so it holds even
# if merging silently stopped working -- and a `tool cat` that stops printing
# `color=` (renamed field, truncated output, hard failure) makes both sides 0,
# which also passes. Two extra assertions turn it into a real check: the oracle
# counted something, and the capped run is STRICTLY coarser, i.e. at least one
# pair actually merged under the cap.
[ "$CAP" -gt 0 ] || fail "count_colors read no colors at all -- the oracle broke, not the coloring"
[ "$RAW" -ge "$CAP" ] || fail "raw cones ($RAW colors) must not be coarser than a capped merge ($CAP)"
[ "$RAW" -gt "$CAP" ] || fail "a 1M max_gate merged NOTHING on hier_seq (raw $RAW == capped $CAP) -- the merge is inert"
echo "PASS: max_gate=0 is raw cones ($RAW colors) vs capped ($CAP)"

# `lhd synth` reaches the same mode through its own namespace, with no kernel or
# harness change: color.* rides straight into pass.color.
D="$W/synth"
mkdir -p "$D"
[ -f "$LIB" ] || fail "missing liberty $LIB"
"$LHD" synth "inou/prp/tests/pyrope/hier_seq.prp" --top hier_seq.top --workdir "$D/w" \
   --set synth.liberty="$LIB" --set color.synth_alg=cones --set color.max_gate=40 \
   --set synth.opentimer=false --set color.absorb=false \
   -q --result-json "$D/r.json" || fail "lhd synth with cones -> $(cat "$D/r.json" 2>/dev/null)"
grep -q '"regions":' "$D/w/synth/qor.json" || fail "lhd synth --set color.synth_alg=cones produced no abc regions"
python3 - "$D/w/synth/qor.json" <<'EOF' || fail "lhd synth cones qor.json has no mapped regions"
import json, sys
q = json.load(open(sys.argv[1]))
assert q["total"]["regions"] > 0, q["total"]
# Every region carries BOTH size estimates, so the predictor stays measurable
# against the mapped gate count on every production run.
assert "pred_aig" in q["total"], q["total"]
for r in q["regions"]:
    assert "pred_aig" in r and "input_ge" in r, r
EOF
echo "PASS: lhd synth --set color.synth_alg=cones maps regions and records pred_aig"

# Negative control: a typo must be refused, not silently colored with `synth`.
if "$LHD" pass color synth --top hier_seq.top --set color.synth_alg=conez lg:"$D/../raw/lg" \
     --workdir "$W/neg" -q --result-json "$W/neg.json" 2>/dev/null; then
  fail "an unknown synth_alg was accepted"
fi
echo "PASS: unknown synth_alg is refused"

echo "PASS: all pass.color cones flows"
