#!/usr/bin/env bash
set -euo pipefail
# VALUE gates for memory commit/hold across clock domains (R13/R15).
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
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/mem_oracle}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
FIXTURE="$OUT/MemOracle.lean"
[[ -x "$TEST_BIN" ]] || { echo "build //pass/lean:design_scan_test first" >&2; exit 2; }

rm -f -- "$FIXTURE"
echo "[mem-oracle] generating $FIXTURE"
LHD_LEAN_MEM_FIXTURE="$FIXTURE" "$TEST_BIN" \
  --gtest_filter='DesignScanClocks.MemoryQuietDomainValueOracle' > "$OUT/generate.log" 2>&1
[[ -s "$FIXTURE" ]] || { echo "[mem-oracle] FAIL: no fixture; see $OUT/generate.log" >&2; exit 1; }

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
[[ "$cases" -gt 0 ]] || { echo "[mem-oracle] FAIL: no value cases" >&2; exit 1; }
# Non-vacuity. The fixture only means anything if the scan produced TWO domains,
# a RAM that kept its MemoryDesc, and the ROM lowered to a constant with its own
# read register -- and if those two registers landed on DIFFERENT clocks. Without
# that last check every value case below would still pass against a model that
# collapsed both onto one domain.
grep -q '{ name := "clk_a" }' "$FIXTURE" && grep -q '{ name := "clk_b" }' "$FIXTURE" \
  || { echo "[mem-oracle] FAIL: certificate is not two-domain" >&2; exit 1; }
grep -q 'nextImg :=' "$FIXTURE" \
  || { echo "[mem-oracle] FAIL: no MemoryDesc -- the RAM did not survive" >&2; exit 1; }
grep -q 'SourceDesc.memConst' "$FIXTURE" \
  || { echo "[mem-oracle] FAIL: no ROM constant" >&2; exit 1; }
if [[ "$(grep -oE 'clock := [0-9]+, asyncReset' "$FIXTURE" | sort -u | wc -l)" -lt 2 ]]; then
  echo "[mem-oracle] FAIL: the read registers share one domain; the gates are vacuous" >&2
  exit 1
fi
echo "[mem-oracle] $cases case(s) to decide"

cd "$ROOT/formal/lean"
NEEDED=LeanSemanticPrimitives.Compiler.DirectVsCompiled
echo "[mem-oracle] building $NEEDED"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build "$NEEDED" > "$OUT/lake_build.log" 2>&1 || {
  echo "[mem-oracle] FAIL: lake build; see $OUT/lake_build.log" >&2; exit 1; }
echo "[mem-oracle] elaborating with $("$LAKE" env lean --version 2>/dev/null | head -1)"
if ! nice -n 19 ionice -c 3 "$LAKE" env lean "$FIXTURE" > "$OUT/elaborate.log" 2>&1; then
  echo "[mem-oracle] FAIL: see $OUT/elaborate.log" >&2
  grep -m5 'error' "$OUT/elaborate.log" >&2
  exit 1
fi
grep -q 'sorry' "$OUT/elaborate.log" && { echo "[mem-oracle] FAIL: sorry reached the oracle" >&2; exit 1; }
echo "[mem-oracle] PASS: all $cases case(s) decided"
