// Golden for varargs_add.prp — the fixed-arity equivalent of `add1(...x)`
// inlined with three u8 actuals. z = a + b + c, widened to u9.
module \varargs_add.top (
  input      [7:0] a,
  input      [7:0] b,
  input      [7:0] c,
  output     [8:0] z
);
  assign z = a + b + c;
endmodule
