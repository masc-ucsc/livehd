// Golden for comb_tuple_out_branch.prp: all four callees compute the same
// function of (x, s).
module top (
  input  [3:0] x,
  input        s,
  output [3:0] sa, output sb,
  output [3:0] da, output db,
  output [3:0] fa, output fb,
  output [3:0] ma, output mb
);
  wire [3:0] a = s ? (x ^ 4'd1) : x;
  wire       b = s & (x == 4'd3);
  assign sa = a;  assign sb = b;
  assign da = a;  assign db = b;
  assign fa = a;  assign fb = b;
  assign ma = a;  assign mb = b;
endmodule
