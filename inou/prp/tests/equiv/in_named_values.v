module \in_named_values.p (input [3:0] x, input [3:0] y, output hit);
  assign hit = (x == 1) || (x == y) || (x == 3);
endmodule
