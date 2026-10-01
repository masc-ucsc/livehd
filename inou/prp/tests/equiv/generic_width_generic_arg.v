// Golden for generic_width_generic_arg.prp: N = 8, so the first inner
// instance carries all 8 bits of `a` and the second (u10) all 10 bits of
// `a*4 + 3`.
module generic_width_generic_arg(input [7:0] a, output [7:0] y, output [9:0] z);
  assign y = a;
  assign z = {a, 2'b11};
endmodule
