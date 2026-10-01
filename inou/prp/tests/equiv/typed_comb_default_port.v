// Golden: the defaulted input is an ordinary port; nothing drives it from the
// default inside the module.
module add_dflt(input [7:0] a, input [7:0] b, output [8:0] r);
  assign r = a + b;
endmodule
