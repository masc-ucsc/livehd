#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A multi-chunk SIGNED operand must be assembled from FINITE SELECTIONS and
# signed ONCE, on the completed result -- never by signing its top chunk.
#
# Concat's contract is that each lane occupies its own declared window and the
# assembled value is "always non-negative: the declared result width is exactly
# the sum of the positional lane widths" (create_pick_concat_dpin). Signing the
# top chunk as well applied the sign twice: a 1-bit top chunk became
# `Sext(x, 1)`, i.e. the 1-bit SIGNED {-1, 0} rather than the unsigned bit that
# process_get_mask and set_bw_1bit both spell out ("a set bit is the unsigned 1;
# only `#sext` may be negative").
#
# Why that is not cosmetic: mixed with ordinary 1-bit unsigned values [0..1] in
# a selection, the union is [-1..1], which needs two SIGNED bits. The value then
# stamps 2 bits and `graph_util::concat_lane_violation` refuses it for driving a
# 1-bit lane -- which is exactly how CORE-ET minion_dcache_top failed, at
# `mux_95536` into lane 0 of `concat_223984` and `mux_94508` into lane 0 of
# `concat_264964` (one defect, two routes).
#
# The inferred width and the refusal are both CORRECT and are deliberately left
# alone: a u8/s8 union really does need 9 bits
# (CpropOpSharing.OutputNarrowingDoesNotNarrowSelectedOperands) and an over-wide
# lane really is an internal compile error
# (BitwidthInfer.OversizedConcatLaneReportsError). A SINGLE-chunk signed read
# also keeps its sign, because there is no assembled result to carry it and the
# sign is observable -- inou/yosys/tests/issue_047.v is that case.
#
# This test reads the EMITTED netlist, where cgen prints a signed net as
# `reg signed` and an unsigned finite selection with cgen's `_u` suffix, so the
# two shapes are distinguishable without a graph dump.
set -u
W="${TEST_TMPDIR:-/tmp/concat_lane_sign_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/mixed.v" <<'V'
module mixed_sign_lane(
  input               sel,
  input               u,
  input  signed [2:0] a,
  input         [2:0] b,
  output        [3:0] z,
  output signed [4:0] y
);
  // A MULTI-CHUNK signed operand: chunks {b[1:0]} and {a[2]}, read signed.
  wire signed [2:0] packed_val = $signed({a[2], b[1:0]});
  assign y = packed_val + 5'sd1;
  // The same bit in a 1-bit selection mixed with an unsigned 1-bit value,
  // driving a ONE-BIT lane -- the shape that turns the extra bit into a refusal.
  wire signed [0:0] top = a[2:2];
  wire m = sel ? top : u;
  assign z = {m, b};
endmodule
V

"$LHD" compile verilog "$W/mixed.v" --reader yosys-slang --top mixed_sign_lane \
  --workdir "$W/work" --emit-dir "verilog:$W/out" >"$W/compile.log" 2>&1 \
  || fail "compile of the mixed-sign design did not succeed; see $W/compile.log"

NET=$(ls "$W"/out/*.v 2>/dev/null | head -1)
[[ -n "$NET" && -s "$NET" ]] || fail "no netlist emitted under $W/out"

# The assembled value must still be sign-extended: the sign is MOVED, not lost.
grep -qE '^[[:space:]]*sext_[0-9]+[[:space:]]*=[[:space:]]*\$signed\(concat_[0-9]+\)' "$NET" \
  || { sed -n '1,80p' "$NET" >&2; fail "the assembled concat is no longer sign-extended -- the sign was dropped, not relocated"; }

# No lane of that concat may be a signed net. Check every concat assignment.
while IFS= read -r line; do
  rhs=${line#*\{}
  rhs=${rhs%%\}*}
  if grep -qE '(^|[^a-zA-Z0-9_])sext_[0-9]+' <<<"$rhs"; then
    echo "offending concat: $line" >&2
    sed -n '1,80p' "$NET" >&2
    fail "a Concat lane is driven by a SIGNED sext net; lanes must be finite selections"
  fi
done < <(grep -E '^[[:space:]]*concat_[0-9]+[[:space:]]*=[[:space:]]*\{' "$NET")

echo "PASS: signed multi-chunk operand assembled from finite selections, signed once on the result"
