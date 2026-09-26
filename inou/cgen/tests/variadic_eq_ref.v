// Integer equality of three values, written as independent binary comparisons.
module eq_variadic(input signed [1:0] a, input [127:0] b,
                   input signed [2:0] c,
                   output eq0, eq1, eq2, eq3, eq4, eq5, eq6, eq7, eq8);
  wire same = ($signed(a) == $signed({1'b0, b})) && ($signed(a) == $signed(c));
  assign {eq0, eq1, eq2, eq3, eq4, eq5} = {6{same}};
  assign eq6 = ($signed(a) < $signed({1'b0, b})) && ($signed(c) < $signed({1'b0, b}));
  assign eq7 = ($signed(a) > $signed({1'b0, b})) && ($signed(c) > $signed({1'b0, b}));
  assign eq8 = ($signed(a) == $signed({1'b0, b}));
endmodule
