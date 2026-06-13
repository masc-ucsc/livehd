module \generic_mod.top (
  input      [7:0]  x,
  input      [7:0]  y,
  input      [15:0] w,
  input      [15:0] v,
  output     [7:0]  s8,
  output     [15:0] s16
);

  assign s8  = x ^ y;
  assign s16 = w ^ v;

endmodule
