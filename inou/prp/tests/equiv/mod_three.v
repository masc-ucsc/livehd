// Golden for mod_three: native Verilog `% 3`. The pyrope side lowers it to a
// base-4 digit-sum reduction (tolg lower_mod3); lgcheck proves bit-exact.
module \mod_three.foo (
  input  [3:0]  a,
  input  [15:0] w,
  output [1:0]  r4,
  output [1:0]  r16
);
  assign r4  = a % 3;
  assign r16 = w % 3;
endmodule
