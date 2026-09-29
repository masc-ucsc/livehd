#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# SystemVerilog `$past(x, n)` and the edge functions built on it ($rose, $fell,
# $stable, $changed) in DESIGN source.
#
# The history is a real flop chain in the design, not a monitor-side trick. That
# distinction is the whole design of this feature:
#
#   * a flop inside a COMBINATIONAL monitor would be a fresh free symbol every
#     step, which silently refutes tautologies -- which is why the Pyrope
#     formal-block path resolves `past` by indexing the unroll instead;
#   * here the state belongs to the DESIGN, so the BMC/induction engine models
#     it like any other register.
#
# The checks below are therefore not just "does it parse": a true claim over
# $past must PROVE, and the false twin must REFUTE with a trace. If the history
# chain were wired up wrong (stale by one, or shifted twice in a cycle) the
# depth-2 case is what catches it.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sv_past_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A deterministic internal reset from a declared init value, so the design
# starts in a known state without a free reset input.
RST='  reg [1:0] rc = 2'"'"'d0;
  reg       rst_d = 1'"'"'b1;
  reg       rst_dd = 1'"'"'b1;
  wire      rst = (rc != 2'"'"'d3);
  always @(posedge clk) begin
    if (rc != 2'"'"'d3) rc <= rc + 2'"'"'d1;
    rst_d  <= rst;
    rst_dd <= rst_d;
  end'

# ---- $past at depth 1 and 2 -------------------------------------------------
cat >"$W/good.sv" <<EOF
module good(input clk, input [7:0] d, output reg [7:0] q1, output reg [7:0] q2);
$RST
  always @(posedge clk) begin
    q1 <= d;
    q2 <= q1;
  end
  always @(posedge clk) if (!rst && !rst_d)  assert (q1 == \$past(d));
  // depth 2 exercises the CHAIN: a stage that shifted twice in one cycle, or
  // one stage short, fails here and not at depth 1.
  always @(posedge clk) if (!rst && !rst_d && !rst_dd) assert (q2 == \$past(d, 2));
endmodule
EOF
OUT="$W/good.out"
"$LHD" formal verify "$W/good.sv" --top good --set formal.bound=20 --workdir "$W/wg" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a true \$past claim must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN: $(cat "$OUT")"

# ---- the false twin must REFUTE --------------------------------------------
# Off by one: q1 is d delayed ONE cycle, so claiming it equals two cycles back
# is false. Without this the test would pass on an implementation that returns
# the current value for every depth.
cat >"$W/bad.sv" <<EOF
module bad(input clk, input [7:0] d, output reg [7:0] q1);
$RST
  always @(posedge clk) q1 <= d;
  always @(posedge clk) if (!rst && !rst_d && !rst_dd) assert (q1 == \$past(d, 2));
endmodule
EOF
OUT="$W/bad.out"
"$LHD" formal verify "$W/bad.sv" --top bad --set formal.bound=20 --workdir "$W/wb" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a false \$past claim must refute: $(cat "$OUT")"
grep -q 'REFUT' "$OUT" || fail "expected REFUTED: $(cat "$OUT")"

# ---- the edge functions are sugar over one cycle of history ------------------
# State the identities and prove them, so a wrong polarity ($rose lowered as
# $fell) cannot pass.
cat >"$W/edges.sv" <<EOF
module edges(input clk, input a);
$RST
  always @(posedge clk) if (!rst && !rst_d) begin
    assert (\$rose(a)    == (a && !\$past(a)));
    assert (\$fell(a)    == (!a && \$past(a)));
    assert (\$stable(a)  == (a == \$past(a)));
    assert (\$changed(a) == (a != \$past(a)));
  end
endmodule
EOF
OUT="$W/edges.out"
"$LHD" formal verify "$W/edges.sv" --top edges --set formal.bound=20 --workdir "$W/we" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "the edge identities must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the edge identities: $(cat "$OUT")"

# ---- a rise is not a fall ---------------------------------------------------
cat >"$W/rf.sv" <<EOF
module rf(input clk, input a);
$RST
  always @(posedge clk) if (!rst && !rst_d) assert (\$rose(a) == \$fell(a));
endmodule
EOF
OUT="$W/rf.out"
"$LHD" formal verify "$W/rf.sv" --top rf --set formal.bound=20 --workdir "$W/wr" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "\$rose == \$fell must refute: $(cat "$OUT")"
grep -q 'REFUT' "$OUT" || fail "expected REFUTED for rose == fell: $(cat "$OUT")"

# ---- refused, not guessed ---------------------------------------------------
# An expression argument would have to be re-lowered in the epilogue where its
# operands may not be live, so it is refused rather than approximated.
cat >"$W/expr.sv" <<EOF
module expr(input clk, input a, input b);
$RST
  always @(posedge clk) if (!rst) assert (\$past(a && b) == \$past(a));
endmodule
EOF
OUT="$W/expr.out"
"$LHD" formal verify "$W/expr.sv" --top expr --workdir "$W/wx" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "\$past over an expression must be refused: $(cat "$OUT")"
grep -qi 'past' "$OUT" || fail "the refusal must name \$past: $(cat "$OUT")"

# ---- ONE signal at TWO depths, and TWO signals at once ----------------------
# The chain is per signal and indexed by depth, so these are where an off-by-one
# in the indexing or a chain shared between the wrong symbols shows up. The
# depth-1 and depth-3 reads below come from the SAME chain; the second signal
# gets its own.
cat >"$W/multi.sv" <<EOF
module multi(input clk, input [7:0] d, input [7:0] e,
             output reg [7:0] d1, output reg [7:0] d2, output reg [7:0] d3, output reg [7:0] e1);
$RST
  reg rst_d3 = 1'b1;
  always @(posedge clk) rst_d3 <= rst_dd;
  always @(posedge clk) begin
    d1 <= d;  d2 <= d1;  d3 <= d2;  e1 <= e;
  end
  always @(posedge clk) if (!rst && !rst_d) begin
    assert (d1 == \$past(d));        // depth 1 off the same chain
    assert (e1 == \$past(e));        // a SECOND signal, its own chain
  end
  always @(posedge clk) if (!rst && !rst_d && !rst_dd && !rst_d3)
    assert (d3 == \$past(d, 3));     // depth 3 off the same chain as depth 1
endmodule
EOF
OUT="$W/multi.out"
"$LHD" formal verify "$W/multi.sv" --top multi --set formal.bound=20 --workdir "$W/wm" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "one signal at two depths + a second signal must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the multi-depth case: $(cat "$OUT")"

# The same design with the depths SWAPPED is false, so the chain cannot be
# returning the same stage for every depth.
cat >"$W/multi_bad.sv" <<EOF
module multi_bad(input clk, input [7:0] d, output reg [7:0] d1, output reg [7:0] d2, output reg [7:0] d3);
$RST
  reg rst_d3 = 1'b1;
  always @(posedge clk) rst_d3 <= rst_dd;
  always @(posedge clk) begin
    d1 <= d;  d2 <= d1;  d3 <= d2;
  end
  always @(posedge clk) if (!rst && !rst_d && !rst_dd && !rst_d3)
    assert (d1 == \$past(d, 3));     // FALSE: d1 is one cycle back, not three
endmodule
EOF
OUT="$W/multi_bad.out"
"$LHD" formal verify "$W/multi_bad.sv" --top multi_bad --set formal.bound=20 --workdir "$W/wmb" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "depth 1 is not depth 3; this must refute: $(cat "$OUT")"
grep -q 'REFUT' "$OUT" || fail "expected REFUTED for the swapped depth: $(cat "$OUT")"

# ---- $past of a REGISTER, and a WIDE $stable/$changed -----------------------
# Everything above samples an input. A register is the other source, and its
# value is settled by the body -- the update must still capture the right one.
# $stable/$changed on a multi-bit value exercises the whole-value compare rather
# than a 1-bit test.
cat >"$W/wide.sv" <<EOF
module wide(input clk, input [7:0] d, output reg [7:0] q, output reg [7:0] q2);
$RST
  always @(posedge clk) begin
    q  <= d;
    q2 <= q;
  end
  always @(posedge clk) if (!rst && !rst_d && !rst_dd) begin
    assert (q2 == \$past(q));                        // \$past of a REGISTER
    assert (\$stable(q)  == (q == \$past(q)));        // wide stable
    assert (\$changed(q) == (q != \$past(q)));        // wide changed
  end
endmodule
EOF
OUT="$W/wide.out"
"$LHD" formal verify "$W/wide.sv" --top wide --set formal.bound=20 --workdir "$W/ww" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "\$past of a register and wide stable/changed must prove: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN for the register/wide case: $(cat "$OUT")"

# NOTE: `$past(x, 0)` is NOT tested because it is not legal SystemVerilog --
# IEEE 1800 requires number_of_ticks >= 1, and slang rejects it before this
# lowering sees it ("'number_of_ticks' argument must be greater than or equal to
# 1"). The depth-0 branch in past_ref is a defensive guard, unreachable from SV
# source; it is reachable from the Pyrope side, which lhd_formal_past_test
# covers.

# ---- a non-literal depth is refused, not guessed ----------------------------
# The chain length has to be known before the body is lowered, so a runtime
# depth cannot be served; refuse rather than silently pick one.
cat >"$W/dyn.sv" <<EOF
module dyn(input clk, input [7:0] d, input [1:0] n);
$RST
  always @(posedge clk) if (!rst) assert (\$past(d, n) == d);
endmodule
EOF
OUT="$W/dyn.out"
"$LHD" formal verify "$W/dyn.sv" --top dyn --workdir "$W/wd" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a non-literal \$past depth must be refused: $(cat "$OUT")"

echo "PASS: SystemVerilog \$past/\$rose/\$fell/\$stable/\$changed lower to a design history chain"
