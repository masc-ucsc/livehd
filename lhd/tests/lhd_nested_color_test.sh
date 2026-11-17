#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A color is one flat graph, and a color may contain another color as a SUBMODULE
# (pass/partition/nest_regions.hpp): `synth.color="inner"` inside a block colored
# "outer" maps as its own region module, instantiated by the outer region's module
# -- not by the def's wrapper, and not inlined into the outer region.
#
# Asserts:
#   1. the wrapper instantiates only the outer region; the outer region
#      instantiates the inner one;
#   2. the nested netlist is LEC-proven against the compiled source;
#   3. a warm rerun on the same workdir hits the region cache for both regions
#      and writes byte-identical Verilog (cached bodies are the un-nested ones,
#      so nesting is applied once per run, never twice).
set -u
LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_nested_color_$$}"
mkdir -p "$W"
fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/nested.prp" <<'EOF'
comb lane::[synth.color="inner"](a:U8,b:U8)->(y:U8) { y=(a+b)#[0..=7] }
pub comb nested::[timecheck=false,synth.color="outer"](a:U8,b:U8,c:U8)->(x:U8,y:U8) {
 x=lane(a=a,b=b).y
 y=(a-c)#[0..=7]
}
EOF

synth() {  # <verilog dir> <result json>
  "$LHD" synth "$W/nested.prp" --top nested --workdir "$W/w" --emit-dir "verilog:$1" \
    --set synth.liberty="$LIB" --set synth.timing=false --set synth.partition=fine -q --result-json "$2" \
    || fail "synth -> $(cat "$2" 2>/dev/null)"
}

synth "$W/v1" "$W/r1.json"
top_v="$W/v1/nested.nested.v"
outer_v="$W/v1/nested.nested__g_outer.v"
[ -f "$top_v" ] && [ -f "$outer_v" ] && [ -f "$W/v1/nested.nested__g_outer_inner.v" ] || fail "missing region modules: $(ls "$W/v1")"
grep -q '^nested__g_outer ' "$top_v" || fail "the wrapper must instantiate the outer region: $(cat "$top_v")"
grep -q '^nested__g_outer_inner ' "$top_v" && fail "the wrapper must not instantiate the inner region: $(cat "$top_v")"
grep -q '^nested__g_outer_inner ' "$outer_v" || fail "the outer region must instantiate the inner region"
echo "PASS: the inner color is a submodule of the outer color"

"$LHD" pass liberty gensim "$LIB" --emit-dir "lg:$W/models" --workdir "$W/wg" -q >/dev/null || fail "gensim"
"$LHD" lec --impl "lg:$W/w/synth/net" --ref "lg:$W/w/synth/lg" --lib "lg:$W/models" --top nested.nested \
  --set formal.effort=proof --set formal.timeout=60 --workdir "$W/wl" -q --result-json "$W/lec.json" >/dev/null 2>&1
grep -q '"verdict":"proven-full"' "$W/lec.json" || fail "nested netlist not proven: $(cat "$W/lec.json")"
echo "PASS: the nested netlist is LEC-proven"

synth "$W/v2" "$W/r2.json"
python3 - "$W/r2.json" <<'PY' || fail "warm rerun did not hit the region cache: $(cat "$W/r2.json")"
import json, sys
inc = json.load(open(sys.argv[1]))['incremental']['abc']
assert inc['hits'] == 2 and inc['misses'] == 0, inc
PY
diff -r -x '*.map' -x manifest.json "$W/v1" "$W/v2" >/dev/null || fail "warm rerun changed the nested netlist"
echo "PASS: warm rerun reuses both regions and nests them identically"

# The color index (<workdir>/synth/color_index.json): every color with its parent
# and source files; a rerun reports how many colors depend only on unchanged sources.
python3 - "$W/w/synth/color_index.json" <<'PY' || fail "color index: $(cat "$W/w/synth/color_index.json")"
import json, sys
d = json.load(open(sys.argv[1]))
regions = {r['module']: r for r in d['regions']}
inner = regions['nested.nested__g_outer_inner']
assert inner['parent'] == 'nested.nested__g_outer' and inner['path'] == ['outer', 'inner'], inner
assert all(r['files'] for r in d['regions']) and d['files'], d
PY
"$LHD" synth "$W/nested.prp" --top nested --workdir "$W/w" --set synth.liberty="$LIB" --set synth.timing=false \
  --set synth.partition=fine -d color-index --diag-fmt pretty >"$W/warm.log" 2>&1 || fail "warm synth: $(tail -5 "$W/warm.log")"
grep -q "color index: 2 of 2 colors depend only on unchanged sources" "$W/warm.log" || fail "warm index: $(cat "$W/warm.log")"
sed -i.bak 's/(a-c)/(a+c)/' "$W/nested.prp"
"$LHD" synth "$W/nested.prp" --top nested --workdir "$W/w" --set synth.liberty="$LIB" --set synth.timing=false \
  --set synth.partition=fine -d color-index --diag-fmt pretty >"$W/edit.log" 2>&1 || fail "edited synth: $(tail -5 "$W/edit.log")"
grep -q "color index: 0 of 2 colors depend only on unchanged sources" "$W/edit.log" || fail "edited index: $(cat "$W/edit.log")"
echo "PASS: the color index records parents and sources and detects the edited colors"
