#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--emit-dir pyrope:DIR` re-emission over a construct-rich source. The
# pyrope: slot turns the coalescer upass on (toln consumers exist), so the
# reduction/popcount/tuple-concat/while/is per-op hooks all run, then
# pass.prp_writer re-emits the units. Checks: the verifier discharges every
# cassert, the emitted top unit holds the expected statements, and a unit
# file exists per lambda.

set -u

LHD=lhd/lhd
PRP=lhd/tests/writer_rich.prp
W="${TEST_TMPDIR:-/tmp/lhd_prp_writer_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$PRP" --emit-dir pyrope:"$W/out/" \
  --set upass.verifier=true --set upass.verifier_pass=7 --set upass.verifier_fail=0 \
  --workdir "$W/w" -q 2>/dev/null \
  || fail "compile with pyrope: emission failed (or verifier count mismatch)"

[ -f "$W/out/manifest.json" ] || fail "pyrope: emission produced no manifest"

TOP="$W/out/writer_rich.prp"
[ -s "$TOP" ] || fail "missing emitted top unit writer_rich.prp"
# The while loop unrolls to its final iteration values.
grep -q 'w = 20' "$TOP" || fail "emitted top unit lost the unrolled while-loop writes"
# Tuple-concat scaffolding survives to the emitted source.
grep -q '(3, 4)' "$TOP" || fail "emitted top unit lost the tuple literal"

# One emitted unit per lambda (rich + helper).
[ -f "$W/out/writer_rich.rich.prp" ] || fail "missing emitted unit for comb rich"
[ -f "$W/out/writer_rich.helper.prp" ] || fail "missing emitted unit for comb helper"

echo "PASS lhd_prp_writer_test"
