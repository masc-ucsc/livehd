// Golden for reduce_xor: Verilog native XOR-reduction (`^`) of the same bit
// slices (parity). `^slice` matches the Pyrope balanced XOR tree regardless of
// tree shape (XOR is associative/commutative).
module \reduce_xor.foo (
  input  [15:0]        a,
  input  signed [15:0] b,
  output               x2,
  output               x4,
  output               x3,
  output               x5,
  output               x7,
  output               xf,
  output               xs,
  output               x1
);
  assign x2 = ^a[1:0];
  assign x4 = ^a[3:0];
  assign x3 = ^a[2:0];
  assign x5 = ^a[4:0];
  assign x7 = ^a[8:2];
  assign xf = ^a;
  assign xs = ^b[9:3];
  assign x1 = a[6];
endmodule
