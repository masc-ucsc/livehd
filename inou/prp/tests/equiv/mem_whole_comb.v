module \mem_whole_comb.combarr (
  input  signed [79:0] inp,
  input  signed  [2:0] idx,
  output signed  [9:0] r,
  output signed [79:0] allout
);
  wire [2:0] uidx = idx;
  assign r      = inp[uidx*10 +: 10];
  assign allout = inp;
endmodule
