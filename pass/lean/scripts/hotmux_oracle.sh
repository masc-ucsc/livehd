#!/usr/bin/env bash
set -euo pipefail

# Independent value-level oracle for the Hotmux lowering (MASTER_INTEGRATION.md
# step 3).
#
# The gtest checks the lowered SHAPE, which was identical through two real
# defects -- a constant control truncated to one bit, then to zero bits. This
# runs the emitted DesignCert through Lean and decides concrete input vectors,
# so a width or priority error fails here even when the structure is right.
#
# The Lean toolchain pin lives in formal/lean, NOT at the repo root, so `lake`
# must be invoked from there or elan resolves a different toolchain.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

OUT="${OUT:-$ROOT/generated/master_port/hotmux_oracle}"
TEST_BIN="${TEST_BIN:-}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
FIXTURE="$OUT/HotmuxOracle.lean"

if [[ -z "$TEST_BIN" ]]; then
  echo "set TEST_BIN to the built pass/lean design_scan_test binary" >&2
  exit 2
fi

echo "[hotmux-oracle] generating $FIXTURE"
LEAN_HOTMUX_FIXTURE="$FIXTURE" "$TEST_BIN" --gtest_filter='DesignScan.HotmuxValueOracle' >"$OUT/generate.log" 2>&1

if [[ ! -s "$FIXTURE" ]]; then
  echo "[hotmux-oracle] FAIL: no fixture produced; see $OUT/generate.log" >&2
  exit 1
fi

cases=$(grep -c '^example' "$FIXTURE" || true)
echo "[hotmux-oracle] $cases value case(s) to decide"

cd "$ROOT/formal/lean"
# Build exactly the module the fixture imports. `lake build` with no target
# builds the DEFAULT target, whose root deliberately does not import Compiler/*
# (it stays light so non-bridge generated files do not pull in Mathlib), so the
# default build leaves CompileDesign.olean absent and the fixture fails with a
# missing object file rather than an unknown module.
NEEDED="$(sed -n 's/^import \(LeanSemanticPrimitives.*\)$/\1/p' "$FIXTURE" | head -1)"
NEEDED="${NEEDED:-LeanSemanticPrimitives.Compiler.CompileDesign}"
OLEAN=".lake/build/lib/lean/${NEEDED//.//}.olean"
if [[ ! -f "$OLEAN" ]]; then
  echo "[hotmux-oracle] building $NEEDED"
  LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" $LAKE build "$NEEDED" >"$OUT/lake_build.log" 2>&1 || {
    echo "[hotmux-oracle] FAIL: lake build $NEEDED; see $OUT/lake_build.log" >&2
    exit 1
  }
fi
echo "[hotmux-oracle] elaborating with $($LAKE env lean --version 2>/dev/null | head -1)"
if $LAKE env lean "$FIXTURE" >"$OUT/lean.log" 2>&1; then
  echo "[hotmux-oracle] PASS: all $cases case(s) decided"
else
  echo "[hotmux-oracle] FAIL: see $OUT/lean.log" >&2
  tail -30 "$OUT/lean.log" >&2
  exit 1
fi

# A decided `native_decide` still rests on ofReduceBool; report it rather than
# leaving the trust story implicit.
if grep -q 'sorry' "$OUT/lean.log"; then
  echo "[hotmux-oracle] FAIL: sorry reached the oracle" >&2
  exit 1
fi
echo "[hotmux-oracle] note: native_decide facts depend on ofReduceBool by construction"
