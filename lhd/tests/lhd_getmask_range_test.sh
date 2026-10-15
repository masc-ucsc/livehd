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

# Pins the Slop backend: the assertions below read the reference C++ kernels
# (HLOP get_mask_op_opt / sra_op), not the default LLVM color objects.
"$LHD" compile "$SRC" --reader slang --top getmask_range \
  --emit-dir sim:"$W/sim" --workdir "$W/work" --set sim.tune.backend=slop -q 2>/dev/null \
  || fail "range-extraction simulator generation failed"

# pass/cprop collapses the source SRA + low mask into one ranged Get_mask.
# HLOP's mixed-width get_mask_op recognizes its contiguous mask and extracts
# only the selected words. The unrelated source-level runtime shift must remain
# an SRA in each of the two scheduled evaluator paths and the pure evaluator.
grep -Rq '::get_mask_op_opt(' "$W/sim" || fail "cprop did not produce a ranged get_mask_op_opt"
[ "$(grep -Roh 'sra_op(' "$W/sim" | wc -l | tr -d ' ')" -eq 3 ] \
  || fail "constant Get_mask range manufactured an SRA (expected only the runtime source shift in all three evaluator paths)"

echo "PASS: cprop collapses SRA/Get_mask to direct HLOP range extraction and true runtime shift remains"
