#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `disable iff (d)` on a concurrent assertion: the property is NOT checked on a
# cycle where `d` holds. The obligation is therefore `!d implies prop`, which as
# a disjunction is `d || prop` -- the implication already supplies the negation.
#
# Why this file exists: the lowering used to or in `!d`, giving `!d || prop`,
# i.e. `d implies prop` -- the exact inverse. It checked the property only WHILE
# the disable condition held. That has two failure modes and this test pins
# BOTH, because only one of them is loud:
#
#   * a true-after-reset property false during reset FALSE-REFUTES (loud);
#   * a property true during reset but FALSE afterwards PASSES VACUOUSLY --
#     reported PROVEN while never once checking the behaviour it is about.
#
# The second is the dangerous one, so the `must_refute` case below is the point
# of the test. A polarity flip makes it pass, and nothing else here would catch
# that.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sv_dis_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# A 3-cycle reset: rc counts to 3, rst is high until it gets there.
RST='  reg [1:0] rc = 2'"'"'d0;
  wire rst = (rc != 2'"'"'d3);
  always @(posedge clk) if (rc != 2'"'"'d3) rc <= rc + 2'"'"'d1;'

# ---- a claim that holds AFTER reset and is false DURING it must PROVE -------
# b is forced to 0 while rst holds and follows a afterwards, so `b == a` is
# false in reset (a is free) and true out of it. With the correct polarity the
# in-reset cycles are discharged and the rest prove. With the inverse, only the
# in-reset cycles are checked and it refutes. b is COMBINATIONAL here so that
# `b == a` is the whole property -- a registered b would lag by a cycle and the
# claim would be false for a reason that has nothing to do with the disable.
cat >"$W/ok.sv" <<EOF
module ok(input clk, input a, output b);
$RST
  assign b = rst ? 1'b0 : a;
  assert property (@(posedge clk) disable iff (rst) b == a);
endmodule
EOF
OUT="$W/ok.out"
"$LHD" formal verify "$W/ok.sv" --top ok --set formal.bound=20 --workdir "$W/wo" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a property disabled during reset must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN: $(cat "$OUT")"

# ---- a claim TRUE during reset and FALSE after must REFUTE -----------------
# This is the vacuity direction. b is 0 throughout reset and 1 once rst clears,
# so `b == 0` holds exactly on the cycles `disable iff (rst)` throws away. The
# inverted lowering checks only those cycles and reports PROVEN.
cat >"$W/bad.sv" <<EOF
module bad(input clk, output reg b);
$RST
  always @(posedge clk) b <= rst ? 1'b0 : 1'b1;
  assert property (@(posedge clk) disable iff (rst) b == 1'b0);
endmodule
EOF
OUT="$W/bad.out"
"$LHD" formal verify "$W/bad.sv" --top bad --set formal.bound=20 --workdir "$W/wb" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a property that is false AFTER reset must refute, not pass vacuously: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED, got: $(cat "$OUT")"

# ---- the disable condition composes with the rest of the property ----------
# Two disable terms and a $past body: the shape the lhdverif assert_concurrent
# probe uses, which the inverted polarity refuted at cycle 2.
cat >"$W/past.sv" <<EOF
module past(input clk, input a, output reg b);
$RST
  reg rst_d = 1'b1;
  always @(posedge clk) rst_d <= rst;
  always @(posedge clk) b <= rst ? 1'b0 : a;
  assert property (@(posedge clk) disable iff (rst || rst_d) b == \$past(a));
endmodule
EOF
OUT="$W/past.out"
"$LHD" formal verify "$W/past.sv" --top past --set formal.bound=20 --workdir "$W/wp" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "disable iff over a \$past body must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the \$past case: $(cat "$OUT")"

echo "PASS: disable iff discharges the property while its condition holds"
