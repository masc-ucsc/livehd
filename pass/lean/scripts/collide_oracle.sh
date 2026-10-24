#!/usr/bin/env bash
set -euo pipefail
# VALUE gates for WRITE-PORT COLLISION ORDERING and per-read forwarding rows.
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
OUT="${OUT:-$ROOT/generated/master_port/collide_oracle}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
LAKE="${LAKE:-lake}"

mkdir -p "$OUT"
[[ -x "$TEST_BIN" ]] || { echo "build //pass/lean:design_scan_test first" >&2; exit 2; }

# TWO fixtures, same gates.  The narrow one is the 2x2 matrix that fits any
# window; the wide one is 2 reads x 32 writes = 64 cells with its HIGHEST cell
# set, which the i64 window could not represent and refused outright.  A wide
# matrix whose value is zero would exercise no high bit, so the wide fixture is
# checked below for the write count that puts a cell past 62, as well as for
# two distinct read rows.
FIXTURES=()
run_fixture() {   # <name> <env-var> <gtest filter>
  local name="$1" var="$2" filt="$3" FIXTURE cases
  FIXTURE="$OUT/$name.lean"
  rm -f -- "$FIXTURE"
  echo "[collide-oracle] generating $FIXTURE"
  env "$var=$FIXTURE" "$TEST_BIN" --gtest_filter="$filt" > "$OUT/generate_$name.log" 2>&1
  [[ -s "$FIXTURE" ]] || { echo "[collide-oracle] FAIL: no fixture; see $OUT/generate_$name.log" >&2; exit 1; }

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
  [[ "$cases" -gt 0 ]] || { echo "[collide-oracle] FAIL: no value cases in $name" >&2; exit 1; }
  # Non-vacuity. These fixtures are about per-(read,write) forwarding structure
  # and which write wins a same-address collision. With one read register the
  # two per-read rows would be one shared value and the disagreement they exist
  # to show could not appear.
  if [[ "$(grep -c 'resetActiveLow' "$FIXTURE")" -lt 2 ]]; then
    echo "[collide-oracle] FAIL: $name has fewer than two read registers; the" >&2
    echo "                 per-read forwarding rows cannot be distinguished" >&2
    exit 1
  fi
  grep -q 'nextImg :=' "$FIXTURE" \
    || { echo "[collide-oracle] FAIL: no MemoryDesc in $name" >&2; exit 1; }
  # The two read registers must be asserted to DIFFERENT values. Equal
  # expectations would pass against a reader that shares one row between reads.
  if [[ "$(grep -o 'nextState\.flops\[[01]\]!) = mk_bv 8 [0-9]*' "$FIXTURE" | sort -u | wc -l)" -lt 2 ]]; then
    echo "[collide-oracle] FAIL: $name does not give the two read registers" >&2
    echo "                 distinct expectations; per-read rows are untested" >&2
    exit 1
  fi
  echo "[collide-oracle] $name: $cases case(s) to decide"
  FIXTURES+=("$FIXTURE")
}

run_fixture CollideOracle     LHD_LEAN_COLLIDE_FIXTURE \
  'DesignScanClocks.TwoWriteTwoReadCollisionValueOracle'
run_fixture WideCollideOracle LHD_LEAN_WIDE_COLLIDE_FIXTURE \
  'DesignScanClocks.WideMatrixHighBitForwardingValueOracle'

# THE WIDE FIXTURE MUST ACTUALLY BE WIDE. 2 reads x 32 writes puts the top cell
# at bit 63, two past the 62 the old window allowed; with fewer writes the
# matrix fits an i64 and tests nothing the narrow fixture does not. The write
# count is read back off the EMITTED certificate rather than assumed.
WIDE="$OUT/WideCollideOracle.lean"
wr=$(grep -o 'MemWrite' "$WIDE" | wc -l)
if [[ "$wr" -lt 32 ]]; then
  echo "[collide-oracle] FAIL: the wide fixture emits $wr write op(s); under 32" >&2
  echo "                 the matrix stays within 62 cells and the high cell" >&2
  echo "                 the i64 window could not reach is never exercised" >&2
  exit 1
fi
echo "[collide-oracle] wide fixture: $wr write op(s), top cell at bit 63"

cd "$ROOT/formal/lean"
NEEDED=LeanSemanticPrimitives.Compiler.DirectVsCompiled
echo "[collide-oracle] building $NEEDED"
LEAN_NUM_THREADS="${LEAN_NUM_THREADS:-8}" "$LAKE" build "$NEEDED" > "$OUT/lake_build.log" 2>&1 || {
  echo "[collide-oracle] FAIL: lake build; see $OUT/lake_build.log" >&2; exit 1; }
echo "[collide-oracle] elaborating with $("$LAKE" env lean --version 2>/dev/null | head -1)"
total=0
for FIXTURE in "${FIXTURES[@]}"; do
  name="$(basename "$FIXTURE" .lean)"
  if ! nice -n 19 ionice -c 3 "$LAKE" env lean "$FIXTURE" > "$OUT/elaborate_$name.log" 2>&1; then
    echo "[collide-oracle] FAIL: $name; see $OUT/elaborate_$name.log" >&2
    grep -m5 'error' "$OUT/elaborate_$name.log" >&2
    exit 1
  fi
  if grep -q 'sorry' "$OUT/elaborate_$name.log"; then
    echo "[collide-oracle] FAIL: sorry reached $name" >&2
    exit 1
  fi
  n=$(grep -c '^example' "$FIXTURE" || true)
  total=$((total + n))
  echo "[collide-oracle] $name: $n case(s) decided"
done
echo "[collide-oracle] PASS: all $total case(s) decided across ${#FIXTURES[@]} fixture(s)"
