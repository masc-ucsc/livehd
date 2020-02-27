module trivial_join(input a, input b, output [1:0] c);
  assign c[0] = a;
  assign c[1] = ~b;
endmodule

