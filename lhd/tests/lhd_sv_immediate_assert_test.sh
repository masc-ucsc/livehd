#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# SystemVerilog IMMEDIATE assertions become design obligations. Contract under
# test:
#   * `assert (expr)` inside a process lowers to the same obligation a Pyrope
#     `assert` produces (LNAST cassert -> fproperty Sub), so a true claim PROVES
#     and a false one REFUTES with a counterexample;
#   * the obligation carries the assertion's SOURCE LOCATION, so a refutation
#     names the line the user wrote;
#   * `assume` is a hypothesis (prove-then-use), classified distinctly from an
#     assert -- not silently promoted to a free constraint;
#   * `cover` is a coverage count, not an obligation: still ignored, but it SAYS
#     so instead of vanishing;
#   * a DEFERRED assertion (`assert #0` / `assert final`) has observed-region
#     simulation semantics this static obligation cannot reproduce, so it is
#     refused with a diagnostic rather than proven as something subtly different.
#
# Why this matters: these used to be dropped on the floor ("synthesis ignores
# immediate assertions"). A design whose every property is an immediate assert
# then verified as "no assert/assert_always obligations found" -- a run that
# proves NOTHING while exiting 0. riscv-formal's generated checks are exactly
# that shape, so the drop was the difference between checking a RISC-V core and
# checking nothing at all.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sv_imm_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A one-deep delay line: y is x delayed a cycle. `y <= 8'hff` is trivially true
# (y is 8 bits); `y == x` is false, since y lags x by a cycle.
cat >"$W/good.sv" <<'EOF'
module good(input clk, input rst, input [7:0] x, output reg [7:0] y);
  always @(posedge clk) y <= rst ? 8'd0 : x;
  always @(posedge clk) begin
    chk_true: assert (y <= 8'hff);
  end
endmodule
EOF

cat >"$W/bad.sv" <<'EOF'
module bad(input clk, input rst, input [7:0] x, output reg [7:0] y);
  always @(posedge clk) y <= rst ? 8'd0 : x;
  always @(posedge clk) begin
    chk_false: assert (y == x);
  end
endmodule
EOF

# ---- a true immediate assert becomes an obligation and PROVES ---------------
OUT="$W/good.out"
"$LHD" formal verify "$W/good.sv" --top good --workdir "$W/wg" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "a true immediate assert must pass: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN: $(cat "$OUT")"
# The regression this test exists for: the assertion must not be DROPPED. A
# dropped one leaves the run with nothing to prove, which is not a pass.
! grep -q 'no assert/assert_always obligations found' "$OUT" \
  || fail "the immediate assert was dropped -- the run proved nothing: $(cat "$OUT")"
# and it must be attributed to the line the user wrote
grep -q 'good.sv:4' "$OUT" || fail "the obligation must carry its source location: $(cat "$OUT")"

# ---- a false one REFUTES with a trace ---------------------------------------
OUT="$W/bad.out"
"$LHD" formal verify "$W/bad.sv" --top bad --workdir "$W/wb" --diag-fmt pretty >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a false immediate assert must fail the run: $(cat "$OUT")"
grep -q 'REFUTED' "$OUT" || fail "expected REFUTED: $(cat "$OUT")"
grep -q 'bad.sv:4' "$OUT" || fail "the refutation must name the assertion's line: $(cat "$OUT")"

# ---- `assume` is a hypothesis, classified apart from an assert ---------------
# Both spellings must reach the engine; an assume that were silently turned into
# an assert (or dropped) would change what the run means.
cat >"$W/asm.sv" <<'EOF'
module asm(input clk, input rst, input [7:0] x, output reg [7:0] y);
  always @(posedge clk) y <= rst ? 8'd0 : x;
  always @(posedge clk) begin
    lim: assume (x < 8'd16);
    chk: assert (y < 8'd16 || rst);
  end
endmodule
EOF
OUT="$W/asm.out"
"$LHD" formal verify "$W/asm.sv" --top asm --workdir "$W/wa" --diag-fmt pretty >"$OUT" 2>&1
grep -qE 'assume' "$OUT" || fail "the assume must appear in the verdict table: $(cat "$OUT")"
! grep -q 'no assert/assert_always obligations found' "$OUT" \
  || fail "assume/assert were both dropped: $(cat "$OUT")"

# ---- `cover` is ignored, but says so ----------------------------------------
cat >"$W/cov.sv" <<'EOF'
module cov(input clk, input [7:0] x, output reg [7:0] y);
  always @(posedge clk) y <= x;
  always @(posedge clk) begin
    c1: cover (x == 8'd7);
  end
endmodule
EOF
OUT="$W/cov.out"
"$LHD" compile verilog --top cov --emit-dir "lg:$W/ocov" --workdir "$W/wc" --diag-fmt pretty -- "$W/cov.sv" >"$OUT" 2>&1 \
  || fail "a cover statement must not fail the compile: $(cat "$OUT")"
grep -qi 'cover' "$OUT" || fail "an ignored cover must be diagnosed, not silent: $(cat "$OUT")"

# ---- a deferred assertion is refused, not silently reinterpreted ------------
cat >"$W/defr.sv" <<'EOF'
module defr(input clk, input [7:0] x, output reg [7:0] y);
  always @(posedge clk) y <= x;
  always @(posedge clk) begin
    d1: assert #0 (y <= 8'hff);
  end
endmodule
EOF
OUT="$W/defr.out"
"$LHD" compile verilog --top defr --emit-dir "lg:$W/odef" --workdir "$W/wd" --diag-fmt pretty -- "$W/defr.sv" >"$OUT" 2>&1
grep -qi 'deferred' "$OUT" || fail "a deferred assertion must be diagnosed: $(cat "$OUT")"

echo "PASS: SystemVerilog immediate assert/assume become design obligations"
