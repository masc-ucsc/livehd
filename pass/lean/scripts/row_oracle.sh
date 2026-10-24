#!/usr/bin/env bash
set -euo pipefail
# VALUE gates for PER-READ forwarding ROWS over one shared array.
#
# The certificate forwards per `fwd_rows[r][w]`: read ordinal r sees write
# ordinal w's new data only when ITS OWN row says so. A whole-cell "this memory
# forwards" flag cannot express that, and nothing STRUCTURAL can tell the two
# apart -- both read ports lower to a `MemRead` either way. So the policy is
# decided here, on values: on the writing step the two ports must DISAGREE.
#
# The certificate comes from the REAL scanner, so this tests what the emitter
# produces rather than a certificate written to pass.
#
# The Lean toolchain pin lives in formal/lean, NOT at the repo root: a bare
# `lean` from anywhere else resolves a different version and every .olean fails
# with "incompatible header".

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/row_oracle}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
FIXTURE="$OUT/RowOracle.lean"
[[ -x "$TEST_BIN" ]] || { echo "build //pass/lean:design_scan_test first" >&2; exit 2; }

rm -f -- "$FIXTURE"
echo "[row-oracle] generating $FIXTURE"
LHD_LEAN_ROW_FIXTURE="$FIXTURE" "$TEST_BIN" \
  --gtest_filter='DesignScanClocks.PerReadRowForwardingValueOracle' > "$OUT/generate.log" 2>&1
[[ -s "$FIXTURE" ]] || { echo "[row-oracle] FAIL: no fixture; see $OUT/generate.log" >&2; exit 1; }

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
[[ "$cases" -gt 0 ]] || { echo "[row-oracle] FAIL: no value cases" >&2; exit 1; }
# Non-vacuity, in the terms this fixture is about.
reads=$(grep -o 'Op_MemRead' "$FIXTURE" | wc -l)
[[ "$reads" -ge 2 ]] \
  || { echo "[row-oracle] FAIL: $reads MemRead node(s); the two ports did not lower separately" >&2; exit 1; }
images=$(grep -o 'SourceDesc.memImg' "$FIXTURE" | wc -l)
[[ "$images" -eq 1 ]] \
  || { echo "[row-oracle] FAIL: $images image source(s); one array must keep ONE state identity" >&2; exit 1; }
grep -q 'nextImg :=' "$FIXTURE" \
  || { echo "[row-oracle] FAIL: no MemoryDesc -- the array did not survive" >&2; exit 1; }
echo "[row-oracle] $cases case(s) to decide over $reads MemRead node(s)"

cd "$ROOT/formal/lean"
NEEDED=LeanSemanticPrimitives.Compiler.DirectVsCompiled
echo "[row-oracle] building $NEEDED"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build "$NEEDED" > "$OUT/lake_build.log" 2>&1 || {
  echo "[row-oracle] FAIL: lake build; see $OUT/lake_build.log" >&2; exit 1; }
echo "[row-oracle] elaborating with $("$LAKE" env lean --version 2>/dev/null | head -1)"
if ! nice -n 19 ionice -c 3 "$LAKE" env lean "$FIXTURE" > "$OUT/elaborate.log" 2>&1; then
  echo "[row-oracle] FAIL: see $OUT/elaborate.log" >&2
  grep -m5 'error' "$OUT/elaborate.log" >&2
  exit 1
fi
grep -q 'sorry' "$OUT/elaborate.log" && { echo "[row-oracle] FAIL: sorry reached the oracle" >&2; exit 1; }
echo "[row-oracle] PASS: all $cases case(s) decided"
