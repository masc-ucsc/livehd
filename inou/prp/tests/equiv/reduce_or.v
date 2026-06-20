// Golden for reduce_or: Verilog native OR-reduction (`|`) of the same bit
// slices. `or_ur`/`or_sr` reduce bits [4:2]; `or_uf`/`or_sf` reduce every bit.
module \reduce_or.foo (
  input  [7:0]        a,
  input  signed [7:0] b,
  output              or_ur,
  output              or_sr,
  output              or_uf,
  output              or_sf
);
  assign or_ur = |a[4:2];
  assign or_sr = |b[4:2];
  assign or_uf = |a;
  assign or_sf = |b;
endmodule
