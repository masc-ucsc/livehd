// :test: roundtrip
// :top: ok_temp
module ok_temp(input clk, input [7:0] a, input [7:0] b, output reg [7:0] q);
  reg [7:0] acc;
  always @(posedge clk) begin
    acc = a + b;
    q  <= acc;
  end
endmodule
