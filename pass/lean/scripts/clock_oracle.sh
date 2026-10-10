#!/usr/bin/env bash
set -euo pipefail
# VALUE oracle for clock-domain resolution (MASTER_INTEGRATION.md R15).
#
# The DesignScanClocks tests check ORDINALS and REFUSALS -- which domain an
# element lands in, and which designs are turned away. None of them says what
# the metadata MEANS when the design is stepped, and a resolver can assign every
# ordinal correctly while a quiet domain still commits.
#
# This takes a certificate from the REAL scanner and has Lean decide concrete
# steps: both domains quiet, each one alone, both together, plus the allEdges
# identity and a short edge vector.
#
# The Lean toolchain pin lives in formal/lean, NOT at the repo root, so `lake`
# must run from there or elan resolves a different toolchain.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/clock_oracle}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
FIXTURE="$OUT/ClockOracle.lean"

[[ -x "$TEST_BIN" ]] || { echo "build //pass/lean:design_scan_test first" >&2; exit 2; }

# Never let a fixture from an earlier binary satisfy this run: a missing gtest
# filter can exit zero without running a test.
rm -f -- "$FIXTURE"
echo "[clock-oracle] generating $FIXTURE"
LEAN_CLOCK_FIXTURE="$FIXTURE" "$TEST_BIN" \
  --gtest_filter='DesignScanClocks.TwoDomainValueOracle' > "$OUT/generate.log" 2>&1

[[ -s "$FIXTURE" ]] || { echo "[clock-oracle] FAIL: no fixture; see $OUT/generate.log" >&2; exit 1; }
cases=$(grep -c '^example' "$FIXTURE" || true)
[[ "$cases" -gt 0 ]] || { echo "[clock-oracle] FAIL: fixture has no value cases" >&2; exit 1; }
# The fixture is only meaningful if the scanner actually resolved TWO domains.
if ! grep -q '{ name := "clk_a" }' "$FIXTURE" || ! grep -q '{ name := "clk_b" }' "$FIXTURE"; then
  echo "[clock-oracle] FAIL: the certificate is not two-domain; the cases prove nothing" >&2
  grep -A4 'clocks   :=' "$FIXTURE" >&2
  exit 1
fi
echo "[clock-oracle] $cases value case(s) to decide"

cd "$ROOT/formal/lean"
NEEDED="$(sed -n 's/^import \(LeanSemanticPrimitives.*\)$/\1/p' "$FIXTURE" | head -1)"
NEEDED="${NEEDED:-LeanSemanticPrimitives.Compiler.CompileDesign}"
echo "[clock-oracle] building $NEEDED"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build "$NEEDED" > "$OUT/lake_build.log" 2>&1 || {
  echo "[clock-oracle] FAIL: lake build $NEEDED; see $OUT/lake_build.log" >&2; exit 1; }
echo "[clock-oracle] elaborating with $("$LAKE" env lean --version 2>/dev/null | head -1)"
if ! nice -n 19 ionice -c 3 "$LAKE" env lean "$FIXTURE" > "$OUT/elaborate.log" 2>&1; then
  echo "[clock-oracle] FAIL: see $OUT/elaborate.log" >&2
  tail -30 "$OUT/elaborate.log" >&2
  exit 1
fi
grep -q 'sorry' "$OUT/elaborate.log" && { echo "[clock-oracle] FAIL: sorry reached the oracle" >&2; exit 1; }
echo "[clock-oracle] PASS: all $cases case(s) decided"
echo "[clock-oracle] note: native_decide facts rest on their per-design native axiom by construction"
