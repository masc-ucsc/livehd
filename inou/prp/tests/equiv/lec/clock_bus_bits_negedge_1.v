/*
:lec_expect: unknown
MUTATED: flop `s` moved from clks[0] to clks[1]. Must never be PROVEN.
*/
module cbn(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] s, output reg [3:0] n);
  always @(posedge clks[1]) s <= d;
  always @(posedge clks[1]) q <= d;
  always @(negedge clks[0]) n <= s;
endmodule
