module out_ssa (
  output [2:0] out,
  output [3:0] out2
);

assign out = 3;
wire   tmp = 1;
assign out2 = out + tmp;


endmodule
