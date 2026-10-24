#!/usr/bin/env bash
set -euo pipefail
# VALUE gates for BYTE ENABLES and read-during-write FORWARDING (R18.5).
#
# These are the cases that only EXIST once clock domains are resolved. In the
# one-clock model every clock fired on every step, so a synchronous reset and an
# asynchronous one were indistinguishable; with a quiet domain they part company:
#
#   sync  -- sampled at the edge, like `din`. Quiet clock => the flop HOLDS.
#   async -- acts with no edge at all, on the visible Q AND the committed state.
#
# The certificate comes from the REAL scanner, so this tests what the emitter
# produces rather than a certificate written to pass.
#
# The Lean toolchain pin lives in formal/lean, NOT at the repo root: a bare
# `lean` from anywhere else resolves a different version and every .olean fails
# with "incompatible header".

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/be_oracle}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
FIXTURE="$OUT/BeOracle.lean"
[[ -x "$TEST_BIN" ]] || { echo "build //pass/lean:design_scan_test first" >&2; exit 2; }

rm -f -- "$FIXTURE"
echo "[be-oracle] generating $FIXTURE"
LHD_LEAN_BE_FIXTURE="$FIXTURE" "$TEST_BIN" \
  --gtest_filter='DesignScanClocks.ByteEnableAndForwardingValueOracle' > "$OUT/generate.log" 2>&1
[[ -s "$FIXTURE" ]] || { echo "[be-oracle] FAIL: no fixture; see $OUT/generate.log" >&2; exit 1; }

# The checked-runtime cases need DirectCheck, which the PRODUCTION emitter does
# not import and should not: `checkDesign`/`checkRuntime` are a validation API,
# not part of what a certificate means. Injected here instead of widening the
# emitter's output for a test's benefit.
python3 -I - "$FIXTURE" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
if 'Compiler.DirectVsCompiled' not in s:
    s = s.replace('import LeanSemanticPrimitives.Compiler.CompileDesign',
                  'import LeanSemanticPrimitives.Compiler.CompileDesign\n'
                  'import LeanSemanticPrimitives.Compiler.DirectCheck\n'
                  'import LeanSemanticPrimitives.Compiler.DirectVsCompiled', 1)
    s = s.replace('open Compiler\n', 'open Compiler\nopen Compiler.Direct\n', 1)
    open(p, 'w').write(s)
PY

cases=$(grep -c '^example' "$FIXTURE" || true)
[[ "$cases" -gt 0 ]] || { echo "[be-oracle] FAIL: no value cases" >&2; exit 1; }
# Non-vacuity. The whole fixture is about a DIFFERENT OPERATOR: memory_lowering
# emits MemWriteBE only once wensize > 1. If the certificate came out with a
# plain MemWrite, every lane assertion below would be testing the single-lane
# path again and would pass while proving nothing about byte enables.
grep -q 'Op_MemWriteBE\|MemWriteBE' "$FIXTURE" \
  || { echo "[be-oracle] FAIL: no MemWriteBE -- wensize did not select the BE path" >&2; exit 1; }
grep -q 'nextImg :=' "$FIXTURE" \
  || { echo "[be-oracle] FAIL: no MemoryDesc" >&2; exit 1; }
echo "[be-oracle] $cases case(s) to decide"

cd "$ROOT/formal/lean"
NEEDED=LeanSemanticPrimitives.Compiler.DirectVsCompiled
echo "[be-oracle] building $NEEDED"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build "$NEEDED" > "$OUT/lake_build.log" 2>&1 || {
  echo "[be-oracle] FAIL: lake build; see $OUT/lake_build.log" >&2; exit 1; }
echo "[be-oracle] elaborating with $("$LAKE" env lean --version 2>/dev/null | head -1)"
if ! nice -n 19 ionice -c 3 "$LAKE" env lean "$FIXTURE" > "$OUT/elaborate.log" 2>&1; then
  echo "[be-oracle] FAIL: see $OUT/elaborate.log" >&2
  grep -m5 'error' "$OUT/elaborate.log" >&2
  exit 1
fi
grep -q 'sorry' "$OUT/elaborate.log" && { echo "[be-oracle] FAIL: sorry reached the oracle" >&2; exit 1; }
echo "[be-oracle] PASS: all $cases case(s) decided"
