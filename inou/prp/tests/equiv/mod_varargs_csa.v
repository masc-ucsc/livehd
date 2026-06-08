// Golden for mod_varargs_csa.prp — the convoluted nested var-arg/template mod
// pair (csa over a per-arg blk_add) is, flattened, a single combinational adder
// of the three u8 inputs widened to u10. blk_add reconstructs each operand
// exactly (sum of its 4-bit blocks == the operand), so z == a + b + c.
module \mod_varargs_csa.top (
  input      [7:0] a,
  input      [7:0] b,
  input      [7:0] c,
  output     [9:0] z
);
  assign z = a + b + c;
endmodule
