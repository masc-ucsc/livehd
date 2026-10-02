/*
:lec_expect: refuted
MUTATED: the memory write moved from clks[0] to clks[1].
*/
module cbb(input [1:0] clks, input we, input [1:0] wa, input [1:0] ra, input [3:0] wd, input d,
           output reg [3:0] q, output reg s);
  reg [3:0] mem [0:3];
  always @(posedge clks[1]) if (we) mem[wa] <= wd;
  always @(posedge clks[1]) q <= mem[ra];
  always @(posedge clks[0]) s <= d;
endmodule
