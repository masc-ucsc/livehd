#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
SRC=inou/prp/tests/equiv/instance_out_struct_ident.prp
LIB=inou/prp/tests/abc/test.lib
CTRL_CONES="${CTRL_CONES:-true}"
run() {
  if [[ "$1" == synth ]]; then
    "$LHD" "$@" --set "pass.color.ctrl_cones=$CTRL_CONES" -q
  else
    "$LHD" "$@" -q
  fi
}
run compile "$SRC" --top top --workdir "$W/default" --result-json "$W/default.json"
run compile "$SRC" --top top --set pass.satopt=true --workdir "$W/explicit" \
  --emit-dir lg:"$W/original" --result-json "$W/explicit.json"
run pass satopt lg:"$W/original" --workdir "$W/prepared" --top top
run pass satopt lg:"$W/original" --workdir "$W/prepared" --top top
grep -q 'reused .* proven mux facts' "$W/prepared"/logs/*pass_satopt*.log
set +e
run pass satopt lg:"$W/original" --top missing_top --workdir "$W/missing" \
  --emit diagnostics:"$W/missing.jsonl" > "$W/missing.log" 2>&1
rc=$?
set -e
[[ "$rc" -gt 0 && "$rc" -lt 128 ]]
grep -q '"code":"top-not-found"' "$W/missing.jsonl"
for mode in on off explicit; do
  extra=()
  [[ "$mode" != off ]] || extra+=(--set pass.abc.satopt=false)
  [[ "$mode" != explicit ]] || extra+=(--set pass.satopt=true)
  run synth "$SRC" --top top --set synth.liberty="$LIB" --set synth.opentimer=false \
    --set synth.threads=1 ${extra[@]+"${extra[@]}"} --emit-dir lg:"$W/$mode-mapped" \
    --workdir "$W/$mode" --result-json "$W/$mode.json"
done
grep -q 'reused .* proven mux facts' "$W/explicit"/logs/*pass_abc*.log
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/model-work"
for mode in on off explicit; do
  run lec --impl lg:"$W/$mode-mapped" --ref lg:"$W/original" --lib lg:"$W/models" \
    --top instance_out_struct_ident.top --set formal.solver=cvc5 --set formal.timeout=60 \
    --workdir "$W/lec-$mode"
done
run synth "$SRC" --top top --set synth.liberty="$LIB" --set synth.opentimer=false \
  --set synth.threads=1 --workdir "$W/on" --result-json "$W/warm.json"
python3 - "$W" "$CTRL_CONES" <<'PY'
import json, pathlib, sys
w = pathlib.Path(sys.argv[1])
def data(name): return json.loads((w / (name + '.json')).read_text())
assert not any(s.startswith('pass.satopt') for s in data('default')['recipe'])
assert any(s.startswith('pass.satopt') for s in data('explicit')['recipe'])
for mode in ('on', 'explicit'):
    facts = sum(r['satopt_facts'] for r in data(mode)['qor']['abc']['regions'])
    # Without control cones this fixture may have no cross-region support.
    # Explicit preparation and automatic preparation must still agree.
    if sys.argv[2] == 'true':
        assert facts > 0
assert sum(r['satopt_facts'] for r in data('on')['qor']['abc']['regions']) == sum(
    r['satopt_facts'] for r in data('explicit')['qor']['abc']['regions'])
assert sum(r['satopt_facts'] for r in data('off')['qor']['abc']['regions']) == 0
assert data('warm')['incremental']['abc']['hits'] > 0
PY
"$LHD" help pass satopt > /dev/null
"$LHD" describe 'pass satopt' > /dev/null
echo "PASS: SAT controls, whole-design LEC, proof and mapping reuse with ctrl_cones=$CTRL_CONES"
