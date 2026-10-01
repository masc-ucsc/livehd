// Independent golden: value's low bit goes to bit 0; x's odd bits survive.
module sparse_bit_write (
  input  [7:0] x,
  input  [3:0] value,
  output [7:0] y
);
  assign y = {x[7], value[3], x[5], value[2], x[3], value[1], x[1], value[0]};
endmodule
