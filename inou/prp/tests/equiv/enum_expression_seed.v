module \enum_expression_seed.p (input [3:0] x, output [3:0] y);
  assign y = x == 4 ? 5 : 3;
endmodule
