// Golden for mod_comp: native Verilog `%` over a computed dividend. The pyrope
// side lowers each `%` to shift/mask (tolg lower_mod); lgcheck proves bit-exact.
module \mod_comp.foo (
  input  [7:0] a,
  input  [7:0] b,
  output [3:0] rp2,
  output [1:0] r3
);
  assign rp2 = (a + b) % 8;
  assign r3  = (a ^ b) % 3;
endmodule
