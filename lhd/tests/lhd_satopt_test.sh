#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.satopt end to end (todo/livehd/2s-satopt A/K): the committed standalone
# pass and the compile opt-in rewrite the graph, the result stays equivalent
# (whole-design LEC against the unoptimized compile), a second run is a no-op,
# proofs are reused under one workdir, stages select the searches, and synthesis
# runs the same engine on its private copy.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
SRC=inou/prp/tests/equiv/instance_out_struct_ident.prp
LIB=inou/prp/tests/abc/test.lib
CTRL_CONES="${CTRL_CONES:-true}"
run() {
  if [[ "$1" == synth ]]; then
    # A tiny fixture: keep its small colors, or no mux would cross a region.
    "$LHD" "$@" --set "pass.color.synth.ctrl_cones=$CTRL_CONES" --set pass.color.synth.min_color_nodes=0 -q
  else
    "$LHD" "$@" -q
  fi
}
lec() {  # lec REF IMPL NAME
  run lec --ref lg:"$1" --impl lg:"$2" --top instance_out_struct_ident.top --set formal.timeout=60 --set pass.satopt=false \
    --workdir "$W/lec-$3" --result-json "$W/lec-$3.json"
}
run compile "$SRC" --top top --workdir "$W/default" --emit-dir lg:"$W/original" --result-json "$W/default.json"
run compile "$SRC" --top top --set pass.satopt=true --workdir "$W/explicit" \
  --emit-dir lg:"$W/compiled" --result-json "$W/compile-on.json"
lec "$W/original" "$W/compiled" compiled
# LEC takes loaded lg: sides as compiled (no satopt by default); source sides
# compile with satopt on, like `lhd synth`.
run lec --ref lg:"$W/original" --impl lg:"$W/compiled" --top instance_out_struct_ident.top \
  --workdir "$W/lec-default" --result-json "$W/lec-default.json"
run formal lec --ref pyrope:"$SRC" --impl pyrope:"$SRC" --top top \
  --workdir "$W/lec-source" --result-json "$W/lec-source.json"

# Standalone: the input lg: is left alone, the optimized design goes to lg:.
run pass satopt lg:"$W/original" --workdir "$W/prepared" --top top --emit-dir lg:"$W/standalone" \
  --result-json "$W/standalone.json"
run pass satopt lg:"$W/original" --workdir "$W/prepared" --top top --emit-dir lg:"$W/standalone2"
grep -q 'reused .* proven mux facts' "$W/prepared"/logs/*pass_satopt*.log
lec "$W/original" "$W/standalone" standalone
# Explicit control-only selection reaches the standalone stage and reports it.
run pass satopt lg:"$W/original" --top top --set pass.satopt.stages=simp_ctrl \
  --emit-dir lg:"$W/ctrl" --workdir "$W/ctrl-work" --result-json "$W/ctrl.json"
lec "$W/original" "$W/ctrl" ctrl
# Idempotent: the optimized design has nothing left to rewrite.
run pass satopt lg:"$W/standalone" --top top --emit-dir lg:"$W/again" --workdir "$W/again-work"
grep -q 'rewrote 0 arm(s)\|stages [a-z,]*: 1 graph(s), 0 changed' "$W/again-work"/logs/*pass_satopt*.log
# Starved (H): every stage stops, keeps only what it proved, says so, and the
# result is still equivalent; the partial proofs are not reused afterwards.
run pass satopt lg:"$W/original" --top top --set pass.satopt.work=1 --workdir "$W/starved-work" \
  --emit-dir lg:"$W/starved" --result-json "$W/starved.json"
lec "$W/original" "$W/starved" starved
run pass satopt lg:"$W/original" --top top --workdir "$W/starved-work" --emit-dir lg:"$W/refilled" \
  --result-json "$W/refilled.json"
# Proof reuse across edits: a comment-only edit reuses every proof (or the
# compile cache restores the optimized graph and satopt does not run), a
# semantic edit proves again.
cp "$SRC" "$W/edit.prp"
run compile "$W/edit.prp" --top top --set pass.satopt=true --workdir "$W/edit" --result-json "$W/edit-cold.json"
printf '\n// a comment-only edit\n' >> "$W/edit.prp"
run compile "$W/edit.prp" --top top --set pass.satopt=true --workdir "$W/edit" --result-json "$W/edit-comment.json"
sed -i.bak 's/d = 2$/d = 3/' "$W/edit.prp"
run compile "$W/edit.prp" --top top --set pass.satopt=true --workdir "$W/edit" --result-json "$W/edit-semantic.json"
# No stages: no rewrite at all.
run pass satopt lg:"$W/original" --top top --set pass.satopt.stages=none --emit-dir lg:"$W/none" \
  --workdir "$W/none-work"
grep -q 'stages none: 1 graph(s), 0 changed' "$W/none-work"/logs/*pass_satopt*.log

set +e
run pass satopt lg:"$W/original" --top missing_top --workdir "$W/missing" \
  --emit diagnostics:"$W/missing.jsonl" > "$W/missing.log" 2>&1
rc=$?
run pass satopt lg:"$W/original" --top top --set pass.satopt.stages=constant --emit-dir lg:"$W/bad" \
  --result-json "$W/bad.json" > "$W/bad.log" 2>&1
bad_rc=$?
run compile "$SRC" --top top --set pass.satopt=true --set pass.satopt.queries=-1 --result-json "$W/badq.json" \
  > "$W/badq.log" 2>&1
badq_rc=$?
set -e
[[ "$badq_rc" -gt 0 && "$badq_rc" -lt 128 ]]
grep -q 'pass.satopt.queries: expects a non-negative decimal integer' "$W/badq.json"
[[ "$rc" -gt 0 && "$rc" -lt 128 ]]
grep -q '"code":"top-not-found"' "$W/missing.jsonl"
[[ "$bad_rc" -gt 0 && "$bad_rc" -lt 128 ]]
grep -q "unknown stage 'constant'" "$W/bad.json"

cat > "$W/satopt-off.toml" <<'EOF'
[pass]
satopt = false
EOF
for mode in on off explicit; do
  extra=()
  [[ "$mode" != off ]] || extra+=(--set pass.satopt=false)
  [[ "$mode" != explicit ]] || extra+=(--config "$W/satopt-off.toml" --set pass.satopt=true)
  run synth "$SRC" --top top --set synth.liberty="$LIB" --set synth.opentimer=false \
    --set synth.threads=1 ${extra[@]+"${extra[@]}"} --emit-dir lg:"$W/$mode-mapped" \
    --workdir "$W/$mode" --result-json "$W/$mode.json"
done
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/model-work"
for mode in on off explicit; do
  run lec --impl lg:"$W/$mode-mapped" --ref lg:"$W/original" --lib lg:"$W/models" --set pass.satopt=false \
    --top instance_out_struct_ident.top --set formal.timeout=60 \
    --workdir "$W/lec-$mode"
done
run synth "$SRC" --top top --set synth.liberty="$LIB" --set synth.opentimer=false \
  --set synth.threads=1 --workdir "$W/on" --result-json "$W/warm.json"
# Synthesis of an lg: input maps what compile produced: no satopt by default.
run synth lg:"$W/original" --top instance_out_struct_ident.top --set synth.liberty="$LIB" --set synth.opentimer=false \
  --set synth.threads=1 --workdir "$W/lgin" --result-json "$W/lgin.json"
python3 - "$W" "$CTRL_CONES" <<'PY'
import json, pathlib, re, sys
w = pathlib.Path(sys.argv[1])
def data(name): return json.loads((w / (name + '.json')).read_text())
def satopt_steps(name): return sum(s.startswith('pass.satopt') for s in data(name)['recipe'])
assert not any(s.startswith('pass.satopt') for s in data('default')['recipe'])
recipe = data('compile-on')['recipe']
# Compile runs satopt after cprop/bitwidth and before pass.formal checks the result.
satopt = next(i for i, s in enumerate(recipe) if s.startswith('pass.satopt'))
formal = next(i for i, s in enumerate(recipe) if s.startswith('pass.formal'))
assert satopt < formal, recipe
for name in ('compiled', 'standalone', 'starved', 'ctrl'):
    assert data('lec-' + name)['lec']['verdict'] == 'proven', name
# The run's stage report is the result's "satopt" member (H).
for name in ('compile-on', 'standalone'):
    stages = data(name)['satopt']['stages']
    assert stages['constants']['state'] == 'completed', (name, stages)
    assert stages['hotmux']['state'] == 'completed' and stages['hotmux']['proven'] > 0, (name, stages)
    # pass.satopt=true runs every stage unless pass.satopt.stages narrows it.
    assert not any(s['state'] == 'disabled' for s in stages.values()), (name, stages)
    assert stages['memory']['state'] == 'inapplicable', (name, stages)
control_stages = data('ctrl')['satopt']['stages']
assert control_stages['simp_ctrl']['state'] == 'completed', control_stages
assert all(s['state'] == 'disabled' for k, s in control_stages.items() if k != 'simp_ctrl'), control_stages
assert 'satopt' not in data('default')
for name, runs in (('lec-default', 0), ('lec-source', 2)):
    result = data(name)
    assert result['lec']['verdict'] == 'proven', name
    assert satopt_steps(name) == runs, result['recipe']
    assert ('satopt' in result) == (runs > 0), name
assert 'satopt' not in data('lec-compiled')
starved = data('starved')['satopt']['stages']
assert starved['hotmux']['state'] == 'exhausted' and starved['hotmux']['applied'] == 0, starved
refilled = data('refilled')['satopt']['stages']
assert refilled['hotmux']['state'] == 'completed' and refilled['hotmux']['reused'] == 0, refilled
assert refilled['hotmux']['applied'] > 0, refilled
cold = data('edit-cold')['satopt']['stages']['hotmux']
assert cold['reused'] == 0 and cold['proven'] > 0, cold
comment = data('edit-comment').get('satopt')
assert comment is None or comment['stages']['hotmux']['reused'] == comment['stages']['hotmux']['proven'] > 0, comment
semantic = data('edit-semantic')['satopt']['stages']['hotmux']
assert semantic['reused'] == 0 and semantic['proven'] > 0, semantic
# `lhd synth foo.prp` is `lhd compile --set pass.satopt=true` then mapping:
# satopt runs in synth's compile step (every stage) and reports in the
# result's "satopt" member; the mapper runs no satopt of its own.
for name in ('on', 'explicit'):
    assert satopt_steps(name) == 1, data(name)['recipe']
    stages = data(name)['satopt']['stages']
    assert not any(s['state'] == 'disabled' for s in stages.values()), stages
    assert sum(s['applied'] for s in stages.values()) > 0, stages
    assert 'satopt' not in data(name)['qor'].get('abc', {}), name
for name in ('off', 'lgin'):
    assert satopt_steps(name) == 0, data(name)['recipe']
    assert 'satopt' not in data(name), name
assert data('warm')['incremental']['abc']['hits'] > 0
PY
"$LHD" help pass satopt > /dev/null
"$LHD" describe 'pass satopt' > /dev/null
echo "PASS: committed satopt (compile + standalone), LEC, idempotence, stages, synthesis with ctrl_cones=$CTRL_CONES"
