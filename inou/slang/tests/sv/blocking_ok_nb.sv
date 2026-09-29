// :test: roundtrip
// :top: ok_nb
module ok_nb(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
