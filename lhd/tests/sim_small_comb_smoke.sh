#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_small_comb_$$}"
SRC=inou/prp/tests/sim/color_small_comb.prp
mkdir -p "$W"
pids=()
for backend in slop llvm; do
  "$LHD" sim "$SRC" --set "sim.tune.backend=$backend" --set sim.checkpoint=false \
    --set compile.upass.inline=false \
    --set lhd.jobs=2 --set sim.tune.profile=off --workdir "$W/$backend" \
    --result-json "$W/$backend.json" -q >"$W/$backend.log" 2>&1 &
  pids+=("$!")
done
for pid in "${pids[@]}"; do wait "$pid"; done
cp "$W/slop/sim/color_small_comb.color_small_comb.color-plan.txt" "$W/slop-plan.txt"

# An explicit frontend-inline request remains supported. Reuse the workdir
# to also check that changing this setting invalidates the compile cache.
"$LHD" sim "$SRC" --setup-only --set compile.upass.inline=true \
  --set sim.checkpoint=false --set sim.tune.profile=off --workdir "$W/slop" \
  --result-json "$W/inline.json" -q >"$W/inline.log" 2>&1

python3 - "$W" <<'PY'
import json, pathlib, re, sys
root = pathlib.Path(sys.argv[1])
name = 'color_small_comb.color_small_comb'
for backend in ('slop', 'llvm'):
    result = json.loads((root / (backend + '.json')).read_text())
    assert result['status'] == 'pass', result
    assert len(result['tests']) == 1 and result['tests'][0]['status'] == 'pass', result
    assert any('inline:false' in step for step in result['recipe']), result['recipe']
# LLVM's hierarchical run retained the hierarchy through uPass. Every phase color
# must contain root, child, and grandchild arithmetic, with no executable Sub.
sim = root / 'llvm/sim'
plan = (sim / (name + '.color-plan.txt')).read_text()
assert plan == (root / 'slop-plan.txt').read_text(), 'backend partition mismatch'
depth = dict(re.findall(r'^site (\S+).* depth=(\d+)', plan, re.M))
versions = dict(re.findall(r'^version-site (\S+) base=(\S+)', plan, re.M))
sub_sites = set(re.findall(r'^site (\S+).* op=sub ', plan, re.M))
assert sub_sites and not sub_sites.intersection(versions.values()), 'executable child call'
colors = re.findall(r'^color .* members=(\S+)', plan, re.M)
assert len(colors) == 2, colors
for members in colors:
    assert {depth[versions[v]] for v in members.split(',')} == {'0', '1', '2'}, members
# Both phase variants reuse one root kernel; ordinary child definitions are
# storage/observation containers and must not emit any native evaluator.
objects = list(sim.glob('*.llvm.o'))
assert len(objects) == 1 and objects[0].name.startswith(name + '.color-kernel-'), objects
for source in sim.glob('*.cpp'):
    if source.name != name + '.cpp':
        assert '::__color_eval(' not in source.read_text(), source
inline = json.loads((root / 'inline.json').read_text())
assert inline['status'] == 'pass', inline
assert any('inline:true' in step for step in inline['recipe']), inline['recipe']
flat = (root / 'slop/sim' / (name + '.color-plan.txt')).read_text()
assert set(re.findall(r'^site .* depth=(\d+)', flat, re.M)) == {'0'}, flat
print('PASS: nested/repeated combinational modules fuse without frontend inlining')
PY
