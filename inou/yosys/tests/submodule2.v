module submodule2 (input a, input b, output [1:0] c);
  z_add foo(.x(a),.y(b),.z(c));
endmodule

module z_add(input x, input y, output [1:0] z);
  assign z = x + y;
endmodule
