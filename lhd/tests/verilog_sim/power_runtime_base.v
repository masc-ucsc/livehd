// A runtime base with a constant exponent: repeated multiplication wrapped to
// the context width (x ** 0 is 1).
module power_runtime_base(input clock, input reset, input signed [4:0] a, input [6:0] b,
                          output signed [7:0] p3, output [9:0] q2, output [3:0] z0, output signed [11:0] n1);
  assign p3 = a ** 3;
  assign q2 = b ** 2;
  assign z0 = b ** 0;
  assign n1 = a ** 1;
endmodule
