#!/usr/bin/env bash
set -euo pipefail

# Self-contained emitter fixtures: no benchmark checkout or external RTL.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/nary_bridge_check}"
: "${TEST_BIN:?set TEST_BIN to the built pass/lean legacy_model_test binary}"
LAKE="${LAKE:-lake}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
CASE_DIR="$(mktemp -d "$OUT/fixtures.XXXXXXXX")"
export TMPDIR="$CASE_DIR/runtime_tmp" TMP="$CASE_DIR/runtime_tmp" TEMP="$CASE_DIR/runtime_tmp"
mkdir -p "$TMPDIR"
LEAN_NARY_BRIDGE_OUTPUT="$CASE_DIR" LEAN_OR_BRIDGE_OUTPUT="$CASE_DIR" "$TEST_BIN" \
  --gtest_filter=LegacyModel.NaryMixedWidthBridgeFixtures:LegacyModel.LargeOrBridgeFixtures >"$CASE_DIR/generate.log" 2>&1
fixtures=("$CASE_DIR/"*_Lgraph.lean)
if [[ "${#fixtures[@]}" != 14 ]]; then
  echo "FAIL: expected 14 fresh fold fixtures; see $CASE_DIR/generate.log" >&2
  exit 1
fi
cd "$ROOT/formal/lean"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build \
  LeanSemanticPrimitives.Translation.NaryBridge LeanSemanticPrimitives.Translation.OrBridge \
  LeanSemanticPrimitives.Translation.LegacyCertWF \
  >"$CASE_DIR/library.log" 2>&1
for fixture in "${fixtures[@]}"; do
  base="$(basename "$fixture" _Lgraph.lean)"
  for theorem in graphCert_wf comb_refines_fast; do
    grep -q "^#print axioms ${base}_${theorem}$" "$fixture"
  done
  if ! /usr/bin/time -v -o "$CASE_DIR/$base.time" "$LAKE" env lean -j 4 "$fixture" \
      >"$CASE_DIR/$base.log" 2>&1; then
    echo "FAIL: $base; see $CASE_DIR/$base.log" >&2
    exit 1
  fi
  if grep -q 'sorry' "$CASE_DIR/$base.log"; then
    echo "FAIL: sorry in $base" >&2
    exit 1
  fi
  for theorem in graphCert_wf comb_refines_fast; do
    grep -q "'${base}_Lgraph.${base}_${theorem}' .*axioms" "$CASE_DIR/$base.log"
  done
  echo "PASS: $base (full WF and all-input bridge)"
done
echo "All 14 fold fixtures passed; logs and time/RSS: $CASE_DIR"
echo "WF/graph checks use native_decide; the general fold lemmas use no additional axioms."
