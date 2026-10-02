/*
:lec_top: cphb
Bus-bit variant of clock_phase_root_clash_hier. The top drives the child's
clock ports from two BITS of one bus (`clks[0]`, `clks[1]`). The design-wide
clock forest resolved every child port through the bit select to the WHOLE
input, so both ports got root `clks`, both sides' phase plans keyed `clks`, and
moving every flop from `a` (clks[0]) to `b` (clks[1]) (_1) was PROVEN under the
default hierarchical LEC. _2 is the unmutated control.
*/
module cphb_sub(input a, input b, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge a) q <= d;
  always @(negedge a) n <= q;
endmodule
module cphb(input [1:0] clks, input [3:0] d, output [3:0] q, output [3:0] n);
  cphb_sub u(.a(clks[0]), .b(clks[1]), .d(d), .q(q), .n(n));
endmodule
