/*
Equivalent: clks[0] respelled through a named wire.
*/
module cbl(input [1:0] clks, input en, input [3:0] d, output reg [3:0] q, output reg [3:0] s, output logic [3:0] l);
  wire c0 = clks[0];
  always_latch if (en) l <= d;
  always @(posedge c0) q <= d;
  always @(posedge c0) s <= d;
endmodule
