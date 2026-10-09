#!/usr/bin/env bash
set -euo pipefail

# Generated fixtures only; no external benchmark inputs.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/large_record_check}"
: "${TEST_BIN:?set TEST_BIN to the built pass/lean legacy_model_test binary}"
LAKE="${LAKE:-lake}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
CASE_DIR="$(mktemp -d "$OUT/fixtures.XXXXXXXX")"
export TMPDIR="$CASE_DIR/runtime_tmp" TMP="$CASE_DIR/runtime_tmp" TEMP="$CASE_DIR/runtime_tmp"
mkdir -p "$TMPDIR"
LEAN_LARGE_RECORD_OUTPUT="$CASE_DIR" "$TEST_BIN" \
  --gtest_filter=LegacyModel.LargeRecordFixtures:LegacyModel.LargeMemoryRecordFixture >"$CASE_DIR/generate.log" 2>&1
cd "$ROOT/formal/lean"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build \
  LeanSemanticPrimitives.Translation.LegacyCertWF >"$CASE_DIR/library.log" 2>&1
for count in 64 65 255 256 534; do
  base="record_$count"
  fixture="$CASE_DIR/${base}_Lgraph.lean"
  test -s "$fixture"
  if ! /usr/bin/time -v -o "$CASE_DIR/$base.time" "$LAKE" env lean -j 4 "$fixture" \
      >"$CASE_DIR/$base.log" 2>&1; then
    echo "FAIL: $base; see $CASE_DIR/$base.log" >&2
    exit 1
  fi
  if grep -q 'sorry' "$CASE_DIR/$base.log"; then
    echo "FAIL: sorry in $base" >&2
    exit 1
  fi
  for role in in out state; do
    for direction in flat nested; do
      grep -q "'${base}_Lgraph.${direction}_${role}_roundtrip' does not depend on any axioms" "$CASE_DIR/$base.log"
    done
  done
  grep -q "'${base}_Lgraph.next_fields' .*axioms" "$CASE_DIR/$base.log"
  if [[ "$count" == 256 ]]; then
    for theorem in graphCert_wf comb_refines_fast next_refines_fast step_refines_fast; do
      grep -q "'${base}_Lgraph.${base}_${theorem}' .*axioms" "$CASE_DIR/$base.log"
    done
  fi
  echo "PASS: $base (record round trips, all next-state fields, executable check)"
done
if ! /usr/bin/time -v -o "$CASE_DIR/record_memory.time" "$LAKE" env lean -j 4 "$CASE_DIR/record_memory_Lgraph.lean" \
    >"$CASE_DIR/record_memory.log" 2>&1; then
  echo "FAIL: memory record; see $CASE_DIR/record_memory.log" >&2
  exit 1
fi
! grep -q 'sorry' "$CASE_DIR/record_memory.log"
grep -q "'memory_default' .*axioms" "$CASE_DIR/record_memory.log"
grep -q "'memory_update' does not depend on any axioms" "$CASE_DIR/record_memory.log"
echo "All six record fixtures passed; logs and time/RSS: $CASE_DIR"
