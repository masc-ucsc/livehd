module hier_tuple4 (
  input run,
  output [2:0] out
);

assign out  = run? 3'd5 : 2;


endmodule
