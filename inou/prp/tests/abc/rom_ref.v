module reference(input clock, en, input [3:0] addr, output [7:0] comb, output reg [7:0] q);
  assign comb = addr*addr + 8'd3*addr + 8'd7;
  always @(posedge clock) if (en) q <= comb;
endmodule
