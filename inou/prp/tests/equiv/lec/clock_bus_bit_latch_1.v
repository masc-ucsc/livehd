/*
:lec_expect: refuted
MUTATED: flop `s` moved from clks[0] to clks[1].
*/
module cbl(input [1:0] clks, input en, input [3:0] d, output reg [3:0] q, output reg [3:0] s, output logic [3:0] l);
  always_latch if (en) l <= d;
  always @(posedge clks[0]) q <= d;
  always @(posedge clks[1]) s <= d;
endmodule
