#!/usr/bin/env bash
set -euo pipefail

LHD="${LHD_BIN:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_packed_child_input_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/design.sv" <<'EOF'
module br_mux_bin (
  input  logic [3:0] select,
  input  logic [15:0][31:0] in,
  output logic [31:0] out,
  output logic        out_valid
);
  assign out = in[select];
  assign out_valid = select < 16;
endmodule

module br_mux_bin_harness (
  input  logic        clk,
  input  logic        rst,
  output wire  [63:0] checksum
);
  logic [63:0] lfsr, sum, nxt, acc;
  logic [31:0] child_out;
  logic        child_valid;

  always_comb begin
    nxt = lfsr ^ (lfsr << 13);
    nxt = nxt ^ (nxt >> 7);
    nxt = nxt ^ (nxt << 17);
    acc = {sum[62:0], sum[63]} ^ 64'(child_out);
    acc = {acc[62:0], acc[63]} ^ 64'(child_valid);
  end

  br_mux_bin dut(
    .select(lfsr[3:0]),
    .in({452'd0, lfsr[63:4]}),
    .out(child_out),
    .out_valid(child_valid)
  );

  always_ff @(posedge clk) begin
    if (rst) begin
      lfsr <= 64'd2611923443488327891;
      sum <= '0;
    end else begin
      lfsr <= nxt;
      sum <= acc;
    end
  end
  assign checksum = sum;
endmodule
EOF

cat >"$W/tb.prp" <<'EOF'
const dut = import("lg:br_mux_bin_harness")

test packed.constant_lane {
  mut acc = dut
  tick 5 {
    acc.rst = clock < 4
    step
  }
  // The first non-reset select is lane 3. It is entirely in the constant-zero
  // portion of the concat, so only out_valid contributes to the checksum.
  // The buggy color refinement read lfsr#[36..=67] and returned 4751061.
  assert(acc.checksum == 1, "constant concat lane stays zero across hierarchy")
}
EOF

"$LHD" compile verilog --top br_mux_bin_harness \
  --emit-dir lg:"$W/lg" --workdir "$W/compile" -q -- "$W/design.sv" \
  || fail "packed child design did not compile"
"$LHD" sim lg:"$W/lg" "$W/tb.prp" --set sim.init_zero=true \
  --set sim.unknown_zero=true --set sim.vcd=false --workdir "$W/sim" -q \
  || fail "packed child simulation selected the wrong concat lane"

echo "PASS: packed child input keeps constant concat lanes at their absolute offsets"
