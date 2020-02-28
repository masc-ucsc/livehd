
module flop(input [7:0] din, input clk, input reset, output reg [7:0] q);

  always @(negedge clk) begin
    q <= din;
  end

endmodule
