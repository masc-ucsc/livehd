/*
:lec_expect: refuted
MUTATED: the flop moved from clks[0] to clks[1].
*/
module cbf(input [1:0] clks, input [3:0] d, output reg [3:0] q);
  always @(posedge clks[1]) q <= d;
endmodule
