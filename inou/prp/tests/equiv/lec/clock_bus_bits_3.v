/*
Equivalent rewrite: the bus bits are named through wires first.
*/
module cbb(input [1:0] clks, input we, input [1:0] wa, input [1:0] ra, input [3:0] wd, input d,
           output reg [3:0] q, output reg s);
  wire c0 = clks[0];
  wire c1 = clks[1];
  reg [3:0] mem [0:3];
  always @(posedge c0) if (we) mem[wa] <= wd;
  always @(posedge c1) q <= mem[ra];
  always @(posedge c0) s <= d;
endmodule
