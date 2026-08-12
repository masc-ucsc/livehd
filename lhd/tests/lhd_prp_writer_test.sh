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

# One .prp per SOURCE FILE: both lambdas of writer_rich.prp land in the one
# emitted writer_rich.prp (below the file-scope statements), and a same-file
# callee takes no import.
grep -q '^pub comb helper(' "$TOP" || fail "missing emitted lambda comb helper"
grep -q '^pub mod rich(' "$TOP" || fail "missing emitted lambda mod rich"
[ -f "$W/out/writer_rich.rich.prp" ] && fail "per-lambda file writer_rich.rich.prp came back"
grep -q 'import("writer_rich' "$TOP" && fail "same-file callee must not be imported"

echo "PASS lhd_prp_writer_test"
