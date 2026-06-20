// Golden for the 1i comb-inliner "single-output result usable in any
// expression context" coverage. one(x) = x+1, and:
//   s = (a+1)+1     = a+2
//   c = ((b+1)==6) ? 7 : 9   = (b==5) ? 7 : 9
//   d = (a+1)+(b+1) = a+b+2
//   e = ((a+1)==5) ? 11 : 13 = (a==4) ? 11 : 13
// (lgcheck compares bit vectors, so the unsigned port decls are immaterial.)
module \comb_single_out_op.csout (
   input  [7:0] a
  ,input  [7:0] b
  ,output [9:0] s
  ,output [8:0] c
  ,output [9:0] d
  ,output [8:0] e
);
  assign s = {2'd0, a} + 10'd2;             // a+2
  assign c = (b == 8'd5) ? 9'd7 : 9'd9;
  assign d = {2'd0, a} + {2'd0, b} + 10'd2; // a+b+2
  assign e = (a == 8'd4) ? 9'd11 : 9'd13;
endmodule
