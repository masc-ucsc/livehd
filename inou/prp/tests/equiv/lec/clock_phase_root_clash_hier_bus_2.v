/*
:lec_expect: proven
Control: unmutated (same root `clks` -- bit 0 -- on both sides through the
forest). PROVEN.
*/
module cphb_sub(input a, input b, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  always @(posedge a) q <= d;
  always @(negedge a) n <= q;
endmodule
module cphb(input [1:0] clks, input [3:0] d, output [3:0] q, output [3:0] n);
  cphb_sub u(.a(clks[0]), .b(clks[1]), .d(d), .q(q), .n(n));
endmodule
