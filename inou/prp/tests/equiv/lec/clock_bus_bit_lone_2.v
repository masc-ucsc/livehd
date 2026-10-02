/*
:lec_expect: refuted
MUTATED: the negedge flop stores ~q. Must REFUTE.
*/
module cbo(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[1]) n <= ~q;
endmodule
