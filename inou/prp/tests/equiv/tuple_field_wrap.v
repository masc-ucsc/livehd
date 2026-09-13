module top(input clk, input [7:0] a, output [3:0] value);
  reg [3:0] \decoded.imm ;
  reg \decoded.valid ;
  always @(posedge clk) begin
    \decoded.imm  <= a[3:0];
    \decoded.valid  <= 1;
  end
  assign value = \decoded.imm ;
endmodule
