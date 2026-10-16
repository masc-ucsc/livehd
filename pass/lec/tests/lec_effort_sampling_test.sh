#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The sampled effort presets (formal.effort=quick-check|test|long-check; `test`
# is the default) SIMULATE their input vectors: a fully pinned trace -- every
# free symbol fixed, the power-on state of a reset-less register file included
# -- is one native evaluation of the unrolled miter (pass/lec/term_sim.hpp),
# not an SMT query. They used to issue one query per trace on a fresh
# non-incremental solver that re-bit-blasted the whole unrolling, with the
# register-file contents left free so a "pinned" query was a real search:
# lhdsuite's dino reached formal.timeout=300 s at `test` (0 vectors reported)
# while a proof took 1.8 s, and never caught its injected ALU bug.
#
# Random input values are biased toward the constants the design DECODES an
# input field against (an opcode compare), so a bug behind one opcode among
# 2^16 is hit at once instead of with probability 2^-16 per cycle.
#
#   1. an equivalent rewrite: TESTED-sampled(N >= 2048), exit 0, simulated
#   2. a bug on one opcode: REFUTED-cex (exit 10) at the default effort
#   3. the same bug at quick-check
set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
W="${TEST_TMPDIR:-/tmp/lec_effort_sampling_$$}"; mkdir -p "$W"; fail=0

cat > "$W/ref.v" <<'EOF'
module m(input clk, input rst, input [31:0] inst, output [31:0] y);
  reg [31:0] regs [0:7];  // reset-less register file: free power-on contents
  reg [31:0] acc;
  wire [15:0] op  = inst[15:0];
  wire [2:0]  rs  = inst[18:16];
  wire [2:0]  rd  = inst[21:19];
  wire [9:0]  imm = inst[31:22];
  always @(posedge clk) begin
    if (rst) acc <= 32'd0;
    else case (op)
      16'h1234: acc <= regs[rs] + {22'd0, imm};
      16'h5a5a: acc <= regs[rs] ^ {22'd0, imm};
      16'h00ff: acc <= acc - 32'd1;
      default:  acc <= acc;
    endcase
    regs[rd] <= acc;
  end
  assign y = acc;
endmodule
EOF
sed "s/acc <= regs\[rs\] ^ {22'd0, imm};/acc <= regs[rs] - {22'd0, imm};/" "$W/ref.v" > "$W/bug.v"
cat > "$W/alt.v" <<'EOF'
module m(input clk, input rst, input [31:0] inst, output [31:0] y);
  reg [31:0] regs [0:7];
  reg [31:0] acc;
  wire [31:0] src = regs[inst[18:16]];
  wire [31:0] im  = {22'd0, inst[31:22]};
  always @(posedge clk) begin
    regs[inst[21:19]] <= acc;
    if (rst) acc <= 32'd0;
    else if (inst[15:0] == 16'h00ff) acc <= acc + 32'hffffffff;
    else if (inst[15:0] == 16'h5a5a) acc <= im ^ src;
    else if (inst[15:0] == 16'h1234) acc <= im + src;
  end
  assign y = acc;
endmodule
EOF
if cmp -s "$W/ref.v" "$W/bug.v"; then echo "FAIL: the bug variant was not injected"; exit 1; fi

run() {  # run <impl> [extra --set ...]; sets OUT and RC
  local impl="$1"; shift
  OUT=$("$LHD" lec --ref "verilog:$W/ref.v" --impl "verilog:$W/$impl" --top m --set formal.simfail=false "$@" 2>&1)
  RC=$?
}
ck() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1 (rc=$RC)"; echo "$OUT" | grep "^lec" | tail -3; fail=1; fi; }

run alt.v
ck "equivalent: TESTED-sampled, exit 0" '[ "$RC" -eq 0 ] && echo "$OUT" | grep -q "^lec: .m. TESTED-sampled("'
n=$(echo "$OUT" | sed -n "s/^lec: .m. TESTED-sampled(\([0-9]*\)).*/\1/p" | head -1)
ck "equivalent: the test level's 2048 vectors" '[ -n "$n" ] && [ "$n" -ge 2048 ]'
ck "equivalent: traces simulated natively" 'echo "$OUT" | grep -q "trace(s) simulated over [0-9]* term node(s)"'
ck "equivalent: the opcode field is decoded" 'echo "$OUT" | grep -q "1 decoded input field(s)"'

run bug.v
ck "opcode bug at the default effort: REFUTED-cex, exit 10" '[ "$RC" -eq 10 ] && echo "$OUT" | grep -q "^lec: .m. REFUTED-cex"'

run bug.v --set formal.effort=quick-check
ck "opcode bug at quick-check: REFUTED-cex, exit 10" '[ "$RC" -eq 10 ] && echo "$OUT" | grep -q "^lec: .m. REFUTED-cex"'

if [ $fail -ne 0 ]; then echo "lec_effort_sampling_test: FAILED"; exit 1; fi
echo "lec_effort_sampling_test: PASSED"
exit 0
