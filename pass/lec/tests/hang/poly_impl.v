// HANG repro #3 (3-way multiply commutativity). Pair with poly_ref.v.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = c * a * b;
endmodule
