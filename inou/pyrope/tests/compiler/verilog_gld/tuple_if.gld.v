module tuple_if_gld (
  output [3:0] out1
);

assign out1 = 2'd2 == 2'd2 ? 4'd10 : 4'd2;

endmodule
