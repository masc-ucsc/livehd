// Every bit of the 62-bit result is written: the low 60 from `a`, bits 60 and
// 61 from `b`. The X seed of the .prp is fully overwritten.
module bitset_chain_typed_unknown (
  input  [61:0] a,
  input  [ 1:0] b,
  output [61:0] out
);

  assign out = {b[1], b[0], a[59:0]};

endmodule
