// NVDLA-style generated bit clocks and packed state. The ascending clock
// range deliberately differs from the descending register/data ranges: bit b is
// clocked by collision_clk[2-b], which the ascending range maps back to gate<b>.
// roundtrip_sim, not lec: native `lhd lec` against equiv/bit_selected_clocks.v
// falsely REFUTES this vector-AND clock form (pre-existing), and a self-LEC
// through the same encoder would share any bit-order misread. So the Pyrope
// round trip is VALUE-checked by bit_selected_clocks_vector_tb.prp, a cycle-level
// model in which each derived bit clock acts as a per-cycle enable. It cannot see
// edge polarity or async-vs-sync clear, which the regex headers pin structurally.
// :test: roundtrip_sim
// :top: bit_selected_clocks
// :verilog_re: always @\(negedge [[:alnum:]_]+ or negedge clr_
// :verilog_re: always @\(posedge [[:alnum:]_]+ or negedge clr_
// :verilog_not_re: always @\([a-z]+edge [[:alnum:]_]+[[:space:]]*\)
module bit_selected_clocks (
  input clk, gate0, gate1, gate2,
  input gate, clr_,
  input [2:0] data, enable,
  output reg [2:0] q
);
  // One ICG per bit: each enable latch is transparent while clk is low, and the
  // vector AND with the clock gates each lane. Selecting collision_clk[2-bw] in
  // the generate loop is a compile-time pick of one of those gated clocks, not a
  // derived clock (the reader resolves the pick: Slang_context::lower_clock_bus_lane).
  reg en0, en1, en2;
  always_latch if (!clk) en0 = gate0 & gate;
  always_latch if (!clk) en1 = gate1 & gate;
  always_latch if (!clk) en2 = gate2 & gate;
  wire [0:2] collision_clk = {en2, en1, en0} & {clk, clk, clk};
  for (genvar bw = 0; bw < 3; bw = bw + 1) begin : bits
    if (bw == 1) begin : falling
      always @(negedge collision_clk[2-bw] or negedge clr_)
        if (!clr_) q[bw] <= 1'b1;
        else if (enable[bw]) q[bw] <= data[bw];
    end else begin : rising
      always @(posedge collision_clk[2-bw] or negedge clr_)
        if (!clr_) q[bw] <= 1'b0;
        else if (enable[bw]) q[bw] <= data[bw];
    end
  end
endmodule
