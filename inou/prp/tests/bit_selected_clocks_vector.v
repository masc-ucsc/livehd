// NVDLA-style generated bit clocks and packed state. The ascending clock
// range deliberately differs from the descending register/data ranges.
module bit_selected_clocks (
  input clk, gate0, gate1, gate2,
  input gate, clr_,
  input [2:0] data, enable,
  output reg [2:0] q
);
  wire [0:2] collision_clk = {gate2, gate1, gate0} & {3{clk & gate}};
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
