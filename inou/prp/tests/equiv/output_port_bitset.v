// Golden for output_port_bitset.prp: each output assembled by concatenation.
module output_port_bitset (
  input  [3:0] a,
  input  [3:0] b,
  input  [3:0] c,
  input  [7:0] d,
  input        s,
  input        t,
  input  [3:0] g,
  input  [1:0] i,
  input        e,
  output [7:0] word,
  output [3:0] rev,
  output [3:0] mixed,
  output [3:0] rt,
  output [7:0] sw,
  output       wgt,
  output [8:0] wp1,
  output       sg,
  output [7:0] sgy
);
  assign word  = {b, a};
  assign rev   = {c[0], c[1], c[2], c[3]};
  assign mixed = {t, a[1:0], s};
  assign rt    = (i == 2'd0) ? {g[3:1], e} :
                 (i == 2'd1) ? {g[3:2], e, g[0]} :
                 (i == 2'd2) ? {g[3], e, g[1:0]} :
                               {e, g[2:0]};
  assign sw    = {d[3:0], d[7:4]};
  assign wgt   = {b, a} > 8'd100;
  assign wp1   = {1'b0, b, a} + 9'd1;
  assign sgy   = {d[3:0], d[7:4]};
  assign sg    = {d[3:0], d[7:4]} > 8'd100;
endmodule
