// NVDLA-style generated bit clocks and packed state. The ascending clock
// range deliberately differs from the descending register/data ranges.
module bit_selected_clocks (
  input clk, gate0, gate1, gate2,
  input gate, clr_,
  input [2:0] data, enable,
  output reg [2:0] q
);
  // One ICG per bit (enable latch transparent while clk is low, then an AND),
  // gathered into an ascending clock bus. `collision_clk[2-bw]` below is a
  // compile-time pick of one of those gated clocks, not a derived clock.
  reg en0, en1, en2;
  always_latch if (!clk) en0 = gate0 & gate;
  always_latch if (!clk) en1 = gate1 & gate;
  always_latch if (!clk) en2 = gate2 & gate;
  wire [0:2] collision_clk = {clk & en2, clk & en1, clk & en0};
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
