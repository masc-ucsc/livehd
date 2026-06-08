// Golden for mod_varargs_add.prp — `add_op` specializes into a combinational
// Sub that computes op + a + b + c; `top` feeds it through. Flattened, the
// behavior is a single u11 adder of the fixed operand plus the three var-args.
module \mod_varargs_add.top (
  input      [7:0]  op,
  input      [7:0]  a,
  input      [7:0]  b,
  input      [7:0]  c,
  output     [10:0] z
);
  assign z = op + a + b + c;
endmodule
