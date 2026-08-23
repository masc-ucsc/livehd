module \comptime_signed_array.top (
  input  signed [3:0] x,
  output signed [3:0] from_range,
  output signed [3:0] from_bits
);
  assign from_range = x;
  assign from_bits  = x;
endmodule
