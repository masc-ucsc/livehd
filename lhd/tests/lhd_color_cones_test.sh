#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `pass.color synth --set synth_alg=cones`
# (todo/livehd/2c-color-synthcones.html): the cone-seeded coloring reaches
# pass.partition and pass.abc through the real CLI, and what comes out the far
# end still computes the same function.
#
#   prp -> lg (inline=false, hierarchy intact)
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

  run compile "$PRP" --top "$TOP" --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top "$TOP" --emit verilog:"$D/ref.v" --workdir "$D/w2"

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
  run compile lg:"$D/part" --top "$TOP" --emit verilog:"$D/post.v" --workdir "$D/w5"

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
# stay LEC-equivalent. `all` is what the CLI picks when `forward` is omitted --
# the runs above -- so `false` is spelled out here to keep the off path covered.
for MODE in false pair all; do
  D="$W/fwd_$MODE"
  mkdir -p "$D"
  run compile "inou/prp/tests/pyrope/hier_seq.prp" --top hier_seq.top --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top hier_seq.top --emit verilog:"$D/ref.v" --workdir "$D/w2"
  run pass color synth --top hier_seq.top --set color.synth_alg=cones --set color.max_gate=40 \
      --set color.forward="$MODE" lg:"$D/lg" --workdir "$D/w3"
  LC_ALL=C grep -raq -- "\"forward\":\"$MODE\"" "$D/lg" || fail "forward=$MODE not recorded in coloring_info"
  run pass partition --top hier_seq.top lg:"$D/lg" --emit-dir lg:"$D/part" --workdir "$D/w4"
  run compile lg:"$D/part" --top hier_seq.top --emit verilog:"$D/post.v" --workdir "$D/w5"
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
run compile "inou/prp/tests/pyrope/hier_seq.prp" --top hier_seq.top --emit-dir lg:"$D/lg" --workdir "$D/w1"
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

# Control duplication uses the same partition/stitch seam in both hier fixtures.
for FIX in hier_comb hier_seq; do
  D="$W/ctrl_$FIX"; TOP="$FIX.top"; mkdir -p "$D"
  run compile "inou/prp/tests/pyrope/$FIX.prp" --top "$TOP" --emit-dir lg:"$D/lg" --workdir "$D/w1"
  run compile lg:"$D/lg" --top "$TOP" --emit verilog:"$D/ref.v" --workdir "$D/w2"
  run pass color synth lg:"$D/lg" --top "$TOP" --set color.synth_alg=cones --set color.ctrl_cones=true --set color.max_gate=40 --workdir "$D/w3"
  run pass partition lg:"$D/lg" --top "$TOP" --emit-dir lg:"$D/part" --workdir "$D/w4"
  run compile lg:"$D/part" --top "$TOP" --emit verilog:"$D/post.v" --workdir "$D/w5"
  run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top "$TOP" --workdir "$D/lec"
done
D="$W/ctrl_shared"; mkdir -p "$D"
cat > "$D/ref.v" <<'VERILOG'
module ctrl_shared(input [31:0] a, b, input [7:0] d, e, output [7:0] y, z);
 wire shared = (a < b);
 wire s1 = shared ^ a[0];
 wire s2 = shared ^ b[1];
 assign y = s1 ? d : e;
 assign z = s2 ? e : d;
endmodule
VERILOG
run compile "$D/ref.v" --top ctrl_shared --emit-dir lg:"$D/lg" --workdir "$D/w1"
# The MERGE assertion needs a cap the merged group fits under: a control group
# above max_gate is partitioned into several colors, which is the budget
# property, not the overlap property. This closure is 228 predicted AIG and
# splits at any cap <= 227, so keep real headroom above it -- a predict_abc_size
# retune must not turn the overlap assertion red. Colour a copy so the
# partition/ABC/LEC leg below keeps its deliberately tiny cap.
cp -R "$D/lg" "$D/lg_merged"
run pass color synth lg:"$D/lg_merged" --top ctrl_shared --set color.synth_alg=cones --set color.max_gate=5000 --workdir "$D/w2m"
LC_ALL=C grep -raq '"mux_groups":1,' "$D/lg_merged" || fail 'overlapping mux selects were not merged'
run pass color synth lg:"$D/lg" --top ctrl_shared --set color.synth_alg=cones --set color.max_gate=40 --workdir "$D/w2"
# The same closure under a cap far below it must PARTITION into two or more
# control colors. Nothing here can assert "the split copied no node": a node
# carries exactly one control color by construction, so the old duplication
# counter was structurally 0. The partition/compile/LEC leg below is what
# proves the split is correct.
LC_ALL=C grep -raqE '"ctrl_colors":\[[0-9]+,' "$D/lg" || fail 'max_gate=40 did not split the oversized control group'
# The default enables groups, while the explicit opt-out remains available.
cp -R "$D/lg" "$D/lg_off"
run pass color synth lg:"$D/lg_off" --top ctrl_shared --set color.synth_alg=cones --set color.ctrl_cones=false --workdir "$D/off"
LC_ALL=C grep -raq '"ctrl_cones":false' "$D/lg_off" || fail 'explicit ctrl_cones=false ignored'
run pass partition lg:"$D/lg" --top ctrl_shared --emit-dir lg:"$D/part" --workdir "$D/w3"
run compile lg:"$D/part" --top ctrl_shared --emit verilog:"$D/post.v" --workdir "$D/w4"
run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top ctrl_shared --workdir "$D/lec"
run synth "$D/ref.v" --top ctrl_shared --workdir "$D/syn" --set synth.liberty="$LIB" --set synth.opentimer=false --set color.synth_alg=cones --set color.max_gate=40 --emit verilog:"$D/mapped.v"
python3 - "$D/syn/synth/qor.json" <<'PY' || fail 'missing control-tier QoR rows'
import json,sys
q=json.load(open(sys.argv[1]));assert any(r['ctrl'] for r in q['regions'])
PY
run pass liberty gensim "$LIB" --emit-dir lg:"$D/models" --workdir "$D/models-work"
run lec --lib lg:"$D/models" --set formal.solver=lgyosys --impl verilog:"$D/mapped.v" --ref verilog:"$D/ref.v" --top ctrl_shared --workdir "$D/mapped_lec"
echo 'PASS: merged mux groups, partition and ABC remain equivalent'


# Mux and enable closures that touch the same decode share ONE overlap group --
# they are no longer two families with the intersection copied into both, so no
# control node is duplicated. The chained mux itself, including PI-selected
# muxes, stays visible to ABC. Match flop state as well.
D="$W/ctrl_families"; mkdir -p "$D"
cat > "$D/ref.v" <<'VERILOG'
module ctrl_families(input clk, input [3:0] a,b, input s,t,
                     input [7:0] d,e, output [7:0] y, output reg [7:0] q,r);
 wire shared = a < b;
 wire first_sel = shared ^ s;
 wire second_sel = shared ^ t;
 wire [7:0] first = first_sel ? d : e;
 assign y = second_sel ? first : (d ^ e);
 always @(posedge clk) begin
   if (first_sel) q <= d;
   if (second_sel) r <= e;
 end
endmodule
VERILOG
run compile "$D/ref.v" --top ctrl_families --emit-dir lg:"$D/lg" --workdir "$D/w1"
# 88 predicted AIG of control: cap above it, so the budget split does not hide
# the property under test (see ctrl_shared above).
cp -R "$D/lg" "$D/lg_merged"
run pass color synth lg:"$D/lg_merged" --top ctrl_families --set color.synth_alg=cones --set color.ctrl_cones=true --set color.max_gate=5000 --workdir "$D/w2m"
LC_ALL=C grep -raq '"mux_groups":1,' "$D/lg_merged" || fail 'mux and enable closures did not share one control group'
LC_ALL=C grep -raq '"enable_groups":0' "$D/lg_merged" || fail 'the enable closure minted a second group instead of joining'
run pass color synth lg:"$D/lg" --top ctrl_families --set color.synth_alg=cones --set color.ctrl_cones=true --set color.max_gate=40 --workdir "$D/w2"
run pass partition lg:"$D/lg" --top ctrl_families --emit-dir lg:"$D/part" --workdir "$D/w3"
run compile lg:"$D/part" --top ctrl_families --emit verilog:"$D/post.v" --workdir "$D/w4"
run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top ctrl_families --workdir "$D/lec"
run synth "$D/ref.v" --top ctrl_families --workdir "$D/syn" --set synth.liberty="$LIB" --set synth.opentimer=false --set color.ctrl_cones=true --set color.synth_alg=cones --set color.max_gate=40 --emit verilog:"$D/mapped.v"
run lec --lib lg:"$W/ctrl_shared/models" --set formal.solver=lgyosys --impl verilog:"$D/mapped.v" --ref verilog:"$D/ref.v" --top ctrl_families --workdir "$D/mapped_lec"
echo 'PASS: mux chains and shared mux/enable control groups remain equivalent after partition and ABC'

# Constant shifts between muxes are internal wiring of the same control group.
# Check the normalizer shape that exposed the fmadd QoR regression through both
# partitioning and mapped equivalence, using the default CLI control policy.
D="$W/ctrl_normalizer"; mkdir -p "$D"
cat > "$D/ref.v" <<'VERILOG'
module ctrl_normalizer(input [15:0] a, output [15:0] y);
  wire [15:0] stage [0:4];
  assign stage[0] = a;
  genvar s;
  generate for (s = 0; s < 4; s = s + 1) begin : norm
    localparam STEP = 1 << (3-s);
    wire zero = ~(|stage[s][15 -: STEP]);
    assign stage[s+1] = zero ? (stage[s] << STEP) : stage[s];
  end endgenerate
  assign y = stage[4];
endmodule
VERILOG
run compile "$D/ref.v" --top ctrl_normalizer --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color synth lg:"$D/lg" --top ctrl_normalizer --set color.synth_alg=cones --workdir "$D/w2"
run pass partition lg:"$D/lg" --top ctrl_normalizer --emit-dir lg:"$D/part" --workdir "$D/w3"
run compile lg:"$D/part" --top ctrl_normalizer --emit verilog:"$D/post.v" --workdir "$D/w4"
run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top ctrl_normalizer --workdir "$D/lec"
run synth "$D/ref.v" --top ctrl_normalizer --workdir "$D/syn" --set synth.liberty="$LIB" --set synth.opentimer=false --emit verilog:"$D/mapped.v"
run pass liberty gensim "$LIB" --emit-dir lg:"$D/models" --workdir "$D/models-work"
run lec --lib lg:"$D/models" --set formal.solver=lgyosys --impl verilog:"$D/mapped.v" --ref verilog:"$D/ref.v" --top ctrl_normalizer --workdir "$D/mapped_lec"
echo 'PASS: default mux groups preserve normalizer wiring through partition and ABC'
