/*
:lec_expect: unknown
:lec_grep: scheduled on clock `clks` but the impl side on `clks\[1\]`
MUTATED: the child's flops moved from port `a` (top `clks[0]`) to port `b`
(top `clks[1]`). Must never be PROVEN.
*/
module cphb_sub(input a, input b, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge b) q <= d;
  always @(negedge b) n <= q;
endmodule
module cphb(input [1:0] clks, input [3:0] d, output [3:0] q, output [3:0] n);
  cphb_sub u(.a(clks[0]), .b(clks[1]), .d(d), .q(q), .n(n));
endmodule
