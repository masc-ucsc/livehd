// Golden for reduce_and: Verilog native AND-reduction (`&`) of the same bit
// slices. `&slice` is 1 iff every selected bit is set; `an_1` reduces one bit.
module \reduce_and.foo (
  input  [7:0]        a,
  input  signed [7:0] b,
  output              an_ur,
  output              an_sr,
  output              an_uf,
  output              an_sf,
  output              an_1
);
  assign an_ur = &a[4:2];
  assign an_sr = &b[4:2];
  assign an_uf = &a;
  assign an_sf = &b;
  assign an_1  = a[5];
endmodule
