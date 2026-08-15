#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=lhd/tests/getmask_range.v
W="${TEST_TMPDIR:-/tmp/lhd_getmask_range_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top getmask_range --recipe O1 \
  --emit-dir sim:"$W/sim" --workdir "$W/work" -q 2>/dev/null \
  || fail "range-extraction simulator generation failed"

# pass/cprop collapses the source SRA + low mask into one ranged Get_mask.
# HLOP's mixed-width get_mask_op recognizes its contiguous mask and extracts
# only the selected words. The unrelated source-level runtime shift must remain
# an SRA in each of the generated evaluator paths.
grep -Rq '::get_mask_op_opt(' "$W/sim" || fail "cprop did not produce a ranged get_mask_op_opt"
[ "$(grep -Roh 'sra_op(' "$W/sim" | wc -l | tr -d ' ')" -eq 2 ] \
  || fail "constant Get_mask range manufactured an SRA (expected only the runtime source shift in both evaluator paths)"

echo "PASS: cprop collapses SRA/Get_mask to direct HLOP range extraction and true runtime shift remains"
