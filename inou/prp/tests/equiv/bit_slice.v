module bit_slice(
  input  [7:0] x,
  output [3:0] lo,
  output [3:0] hi,
  output       bit0,
  output       bit7
);
assign lo   = x[3:0];
assign hi   = x[7:4];
assign bit0 = x[0];
assign bit7 = x[7];
endmodule
