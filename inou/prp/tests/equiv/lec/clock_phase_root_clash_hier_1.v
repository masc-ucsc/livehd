/*
:lec_expect: unknown
:lec_grep: scheduled on clock `c0` but the impl side on `c1`
MUTATED: the child's flops moved from port `a` (top `c0`) to port `b` (top
`c1`). Must never be PROVEN.
*/
module cph_sub(input a, input b, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge b) q <= d;
  always @(negedge b) n <= q;
endmodule
module cph(input c0, input c1, input [3:0] d, output [3:0] q, output [3:0] n);
  cph_sub u(.a(c0), .b(c1), .d(d), .q(q), .n(n));
endmodule
