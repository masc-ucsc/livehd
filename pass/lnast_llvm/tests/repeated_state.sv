// Four copies with different inputs must share executable code while each
// keeps its own history. Both results expose every instance to the oracle.
module repeated_state_lane(input clk, input [7:0] x, output [7:0] a, b);
  reg [7:0] acc = 0, prev = 0;
  always @(posedge clk) begin
    prev <= acc;
    acc <= (acc ^ x) + 8'd3;
  end
  assign a = acc;
  assign b = prev;
endmodule

module repeated_state(input clk, input [31:0] x, output [31:0] a, b);
  for (genvar i = 0; i < 4; ++i) begin : lanes
    repeated_state_lane lane(clk, x[8*i +: 8], a[8*i +: 8], b[8*i +: 8]);
  end
endmodule
